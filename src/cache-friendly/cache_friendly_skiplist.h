#ifndef _CACHE_FRIENDLY_CONCURRENT_SKIPLIST
#define _CACHE_FRIENDLY_CONCURRENT_SKIPLIST

#include <iostream>
#include <vector>
#include <algorithm>
#include <string>
#include <queue>
#include <stack>
#include <set>
#include <iomanip> 
#include <cmath>
#include <map>
#include <random>
#include <thread>
#include <shared_mutex>
#include <array>

#endif

const int ARRAY_SIZE = 5;

struct DefaultLevelGenerator {
    // We keep the RNG state inside the functor or as static 
    // to avoid re-seeding every time we call it.
    std::mt19937 rng{std::random_device{}()};
    std::geometric_distribution<int> dist{0.5};

    int operator()(int maxHeight) {
        return std::min(dist(rng), maxHeight - 1);
    }
};

template <typename K, typename V>
struct SkiplistNode;

template<typename K, typename V>
struct SkiplistData {
    using nodeType = SkiplistNode<K,V>;
    V* val;
    K* key;
    nodeType* down;

    SkiplistData(V* v, K* k, nodeType* n) : val(v), key(k), down(n) {}

    SkiplistData() : val(nullptr), key(nullptr), down(nullptr) {}

    SkiplistData(SkiplistData&& other) noexcept 
        : val(other.val), key(other.key), down(other.down) 
    {
        // Nullify the source pointers so the old destructor doesn't delete them!
        other.val = nullptr;
        other.key = nullptr;
        other.down = nullptr;
    }

    SkiplistData& operator=(SkiplistData&& other) noexcept {
        if (this != &other) {
            // Clean up our own existing memory first
            if(val != nullptr) delete val;
            if(key!=nullptr) delete key;

            // Steal from the other
            val = other.val;
            key = other.key;
            down = other.down;

            // Nullify the other
            other.val = nullptr;
            other.key = nullptr;
            other.down = nullptr;
        }
        return *this;
    }

    ~SkiplistData() {
        if(val!=nullptr) delete val;
        if(key!=nullptr) delete key;
        down = nullptr;
    }
    void clear() {
        if(val!=nullptr) delete val;
        if(key!=nullptr) delete key;
        down = nullptr;
        val = nullptr;
        key = nullptr;
    }
};

template <typename K, typename V>
struct SkiplistNode {
    std::array<SkiplistData<K, V>, ARRAY_SIZE> data;
    uint8_t size;
    SkiplistNode* next;
    std::shared_mutex _mut;

    SkiplistNode(SkiplistNode* n = nullptr) 
        : next(n)
    {
        size = 0;
    }

    ~SkiplistNode() {
        next = nullptr;
        // for(int i=0;i<size;i++){
        //     delete data[i];
        // }
    }

    inline void lock(bool read_only){
        if(read_only){
            _mut.lock_shared();
        } else {
            _mut.lock();
        }
    }

    inline void unlock(bool read_only){
        if(read_only){
            _mut.unlock_shared();
        } else {
            _mut.unlock();
        }
    }
};

template <typename K, typename V, int MAX_HEIGHT = 5, typename LevelGen = DefaultLevelGenerator>
class SimpleSkiplist {
    using dataType = SkiplistData<K,V>;
    using nodeType = SkiplistNode<K,V>;
    private:
    static_assert(MAX_HEIGHT > 1);
    LevelGen _gen;
    nodeType* structure[MAX_HEIGHT];

    template <typename Func, typename... Args>
    void move_down_prev_cur_locked(bool read_only_unlock, bool read_only_lock, nodeType*& cur, int index, Func&& func, Args&&... args){
        std::invoke(std::forward<Func>(func), cur, index, std::forward<Args>(args)...);

        nodeType* t = cur->data[index].down;
        if(t!=nullptr) t->lock(read_only_lock);
        cur->unlock(read_only_unlock);
        cur = t;
    }

    template <typename Func, typename... Args>
    void move_right_prev_cur_locked(bool read_only, nodeType*& cur, nodeType*& prev, Func&& func, Args&&... args){

        std::invoke(std::forward<Func>(func), cur, prev, std::forward<Args>(args)...);

        prev->unlock(read_only);
        prev = cur;
    }

    public:
    SimpleSkiplist() {
        for(int i=0;i<MAX_HEIGHT;i++){
            structure[i] = new nodeType();
            if(i>0){
                structure[i]->data[0].down = structure[i-1];
            }
            structure[i]->size = 1;
        }
        for(int i=0;i<MAX_HEIGHT;i++){
            nodeType* n = new nodeType();
            structure[i]->next = n;
        }
    }

    static void empty_go_down(nodeType*& cur, int index){
        return;
    }

    inline static bool node_start_is_greater(nodeType* cur, K key){
        if(cur == nullptr) return true;
        if(cur->size == 0) return true;
        if(cur->data[0].key == nullptr) return true;
        if(*(cur->data[0].key) > key) return true;
        return false;
    }

    inline static void safe_lock(nodeType* cur, bool read_only){
        if(cur != nullptr) cur->lock(read_only);
    }

    inline static void safe_unlock(nodeType* cur, bool read_only){
        if(cur != nullptr) cur->unlock(read_only);
    }

    std::optional<V> get(K key) {
        structure[MAX_HEIGHT-1]->lock(true);
        nodeType* prev = structure[MAX_HEIGHT-1];
        while(true){
            if(prev == nullptr){
                return std::nullopt;
            }

            bool moved = false;
            for(int i=0;i<prev->size;i++){
                if(prev->data[i].key == nullptr) continue;
                if(*(prev->data[i].key) < key) continue;
                if(*(prev->data[i].key) > key){
                    move_down_prev_cur_locked(true, true, prev, i-1, empty_go_down);
                    moved = true;
                    break;
                }
                if(*(prev->data[i].key) == key){
                    int index = i;
                    while(prev->data[index].down != nullptr){
                        prev->data[index].down->lock(true);
                        int new_index = 0;
                        nodeType* d = prev->data[index].down;
                        while(d->data[new_index].key == nullptr || *(d->data[new_index].key) != key) new_index++;
                        prev->unlock(true);
                        prev = d;
                        index = new_index;
                    }
                    V val = *prev->data[index].val;
                    prev->unlock(true);
                    return val;
                }

            }

            if(moved) continue;
            if(prev->next == nullptr){
                move_down_prev_cur_locked(true, true, prev, prev->size - 1, empty_go_down);
                continue;
            }

            prev->next->lock(true);
            nodeType* next = prev->next;
            if(next->size == 0 || next->data[0].key == nullptr || *(next->data[0].key) > key){
                next->unlock(true);
                move_down_prev_cur_locked(true, true, prev, prev->size - 1, empty_go_down);
                continue;
            }

            move_right_prev_cur_locked(true, next, prev, [](nodeType*&, nodeType*&){});
        }
    }

    static void insert_into_node(nodeType*& cur, int cur_index, K key, int cur_height, V val, nodeType*& next_node, nodeType*& current_insert_node){
        dataType new_val = dataType();
        new_val.key = new K(key);
        new_val.down = next_node;
        if(cur_height == 0) new_val.val = new V(val);

        if(cur->size == ARRAY_SIZE || current_insert_node != nullptr){
            if(current_insert_node == nullptr){
                current_insert_node = new nodeType();
                current_insert_node->lock(false);
            }

            current_insert_node->next = cur->next;
            cur->next = current_insert_node;

            // we have to move all values from key to the new node
            current_insert_node->data[0] = std::move(new_val);
            current_insert_node->size++;
            int j = 1;
            for(int i=cur_index;i<cur->size;i++){
                current_insert_node->data[j] = std::move(cur->data[i]);
                current_insert_node->size++;
                cur->size--;
                j++;
            }
            return;
        }
        
        std::shift_right(cur->data.begin() + cur_index, cur->data.end(), 1);
        cur->data[cur_index] = std::move(new_val);
        cur->size++;
        // current_insert_node = next_node;
    }

    void insert_or_modify(K key, V val) {
        int level = _gen(MAX_HEIGHT);
        int cur_height = MAX_HEIGHT-1;
        bool read_only = true;
        if(level >= cur_height){
            read_only = false;
        }

        structure[MAX_HEIGHT-1]->lock(read_only);
        nodeType* cur = structure[MAX_HEIGHT-1];


        while(cur_height>=(level+1)){
            // search
            // at the end of this, either we are done, or we have reached the height at level, where we need to do our first insert.
            bool next_lock = !(cur_height == (level+1));

            for(int i=0;i<cur->size;i++){
                if(cur->data[i].key == nullptr || *(cur->data[i].key) < key){
                    if(i == (cur->size-1)){
                        safe_lock(cur->next, true);
                        bool greater = node_start_is_greater(cur->next, key);
                        if(greater){
                            safe_unlock(cur->next, true);
                            move_down_prev_cur_locked(true, next_lock, cur, i, empty_go_down);
                            cur_height--;
                        } else {
                            nodeType* next = cur->next;
                            move_right_prev_cur_locked(true, next, cur, [](nodeType*&, nodeType*&){});
                        }
                        break;
                    }
                    continue;
                }
                if(*(cur->data[i].key) > key){
                    move_down_prev_cur_locked(true, next_lock, cur, i-1, empty_go_down);
                    cur_height--;
                    break;
                }
                if(*(cur->data[i].key) == key){
                    int index = i;
                    while(cur->data[index].down != nullptr){
                        if(cur_height == 1) cur->data[index].down->lock(false);
                        else cur->data[index].down->lock(true);

                        int new_index = 0;
                        nodeType* d = cur->data[index].down;
                        while(d->data[new_index].key == nullptr || *(d->data[new_index].key) != key) new_index++;
                        cur->unlock(true);
                        cur = d;
                        index = new_index;
                        cur_height--;
                    }
                    cur->data[index].val = new V(val);
                    cur->unlock(false);
                    return;
                }
            }
        }

        nodeType* next_node = nullptr;
        nodeType* current_insert_node = nullptr;
        // there is no next_node allocated yet, or a current isnert node that we need to care about.

        while(true){
            if(cur_height>0 && next_node == nullptr){
                // allocate next node if there is further to go
                next_node = new nodeType();
                next_node->lock(false);
            }
            // we are at level, we need to do the first insert here.
            int index_found = -1;
            for(int i=0;i<cur->size;i++){
                if(cur->data[i].key == nullptr || *(cur->data[i].key) < key){
                    if(i == (cur->size-1)){
                        safe_lock(cur->next, false);
                        nodeType* next = cur->next;
                        bool greater = node_start_is_greater(cur->next, key);
                        if(greater){
                            safe_unlock(cur->next, false);
                            index_found = i+1;
                            break;
                        } else {
                            move_right_prev_cur_locked(false, next, cur, [](nodeType*&, nodeType*&){});
                        }
                        break;
                    }
                    continue;
                }
                if(*(cur->data[i].key) > key){
                    index_found = i;
                    break;
                }
                if(*(cur->data[i].key) == key){
                    // okay, we have found it on this level. We need to move this to current_insert_node if current_insert_node exists.
                    // then we can clean up next_node and just change the final value.

                    // move to current_insert_node
                    int index = i;
                    if(current_insert_node != nullptr){
                        for(int j=index;j<cur->size;j++){
                            current_insert_node->data[j-index] = std::move(cur->data[j]);
                            cur->size--;
                            current_insert_node->size++;
                        }
                        current_insert_node->next = cur->next;
                        cur->next = current_insert_node;
                        cur->unlock(false);
                        cur = current_insert_node;
                        index = 0;
                    }
                    
                    // delete new_node
                    if(next_node != nullptr){
                        delete next_node;
                    }

                    // go down.
                    while(cur->data[index].down != nullptr){
                        cur->data[index].down->lock(false);
                        int new_index = 0;
                        nodeType* d = cur->data[index].down;
                        while(d->data[new_index].key == nullptr || *(d->data[new_index].key) != key) new_index++;
                        cur->unlock(false);
                        cur = d;
                        index = new_index;
                    }
                    cur->data[index].val = new V(val);
                    cur->unlock(false);
                    return;
                }
            }

            // index_found is the index in cur that we need to do our insert in. is -1 then we are still searching.
            if(index_found != -1){
                insert_into_node(cur, index_found, key, cur_height, val, next_node, current_insert_node);
                // so we have inserted into cur. cur is the node that we need to go down. next_node has to be current_insert_node. current_insert_node has to be discarded.
                // can cur == current_insert_node? no i dont think so.
                safe_unlock(current_insert_node, false);
                current_insert_node = next_node;
                next_node = nullptr;

                if(cur_height == 0){
                    cur->unlock(false);
                    safe_unlock(current_insert_node, false);
                    return;
                }

                index_found = cur->size-1;
                for(int i=0;i<cur->size;i++){
                    if(cur->data[i].key == nullptr) continue;
                    if(*(cur->data[i].key) < key) continue;
                    index_found = i-1;
                    break;
                }
                cur->data[index_found].down->lock(false);
                nodeType* t = cur->data[index_found].down;
                cur->unlock(false);
                cur = t;
                cur_height--;
            }
        }
    }

    static void delete_value(nodeType*& cur, int index, nodeType*& prev){
        cur->data[index].clear();
        if(cur->size>1){
            cur->size--;
            std::shift_left(cur->data.begin()+index, cur->data.end(), 1);
            if(index==0){
                cur->unlock(false);
                cur = prev;
                prev = nullptr;
            } else {
                safe_unlock(prev, false);
                prev = nullptr;
            }
        } else {
            // prev has to be non null
            prev->next = cur->next;
            delete cur;
            cur = prev;
            prev = nullptr;
        }
    }

    bool remove(K key) {
        structure[MAX_HEIGHT-1]->lock(false);
        nodeType* cur = structure[MAX_HEIGHT-1];
        nodeType* prev = nullptr;
        while(true){
            if(cur == nullptr){
                return false;
            }

            for(int i=0;i<cur->size;i++){
                if(cur->data[i].key == nullptr || *(cur->data[i].key) < key){
                    if(i == (cur->size-1)){
                        safe_lock(cur->next, false);
                        bool greater = node_start_is_greater(cur->next, key);
                        if(greater){
                            safe_unlock(cur->next, false);
                            move_down_prev_cur_locked(false, false, cur, i, empty_go_down);
                        } else {
                            nodeType* next = cur->next;
                            safe_unlock(prev, false);
                            prev = cur;
                            cur = next;
                        }
                        break;
                    }
                    continue;
                }
                if(*(cur->data[i].key) > key){
                    move_down_prev_cur_locked(false, false, cur, i-1, empty_go_down);
                    break;
                }
                if(*(cur->data[i].key) == key){
                    delete_value(cur, i, prev);
                }

            }
        }
    }

    static void go_down_range(nodeType*& cur, int index, int& cur_height){
        cur_height--;
        return;
    }


    template <typename F>
    void range(K key, F func, uint64_t length){
        structure[MAX_HEIGHT-1]->lock(true);
        nodeType* prev = structure[MAX_HEIGHT-1];
        int cur_height = MAX_HEIGHT-1;
        while(true){
            bool moved = false;
            bool found = false;
            for(int i=0;i<prev->size;i++){
                if(prev->data[i].key == nullptr) continue;
                if(*(prev->data[i].key) < key) continue;
                if(*(prev->data[i].key) > key){
                    if(cur_height == 0){
                        found = true;
                        break;
                    }
                    move_down_prev_cur_locked(true, true, prev, i-1, go_down_range, cur_height);
                    moved = true;
                    break;
                }
                if(*(prev->data[i].key) == key){
                    int index = i;
                    while(prev->data[index].down != nullptr){
                        prev->data[index].down->lock(true);
                        int new_index = 0;
                        nodeType* d = prev->data[index].down;
                        while(d->data[new_index].key == nullptr || *(d->data[new_index].key) != key) new_index++;
                        prev->unlock(true);
                        prev = d;
                        index = new_index;
                    }
                    found = true;
                    break;
                }

            }

            if(found) break;

            if(moved) continue;
            if(prev->next == nullptr){
                if(cur_height == 0){
                    break;
                }
                move_down_prev_cur_locked(true, true, prev, prev->size - 1, go_down_range, cur_height);
                continue;
            }

            prev->next->lock(true);
            nodeType* next = prev->next;
            if(next->size == 0 || next->data[0].key == nullptr || *(next->data[0].key) > key){
                if(cur_height == 0){
                    prev->unlock(true);
                    prev = next;
                    break;
                }
                next->unlock(true);
                move_down_prev_cur_locked(true, true, prev, prev->size - 1, go_down_range, cur_height);
                continue;
            }

            move_right_prev_cur_locked(true, next, prev, [](nodeType*&, nodeType*&){});
        }
        int index = -1;
        for(int i=0;i<prev->size;i++){
            if(prev->data[i].key == nullptr) continue;
            if(*prev->data[i].key < key) continue;
            index = i;
            break;
        }
        if(index == -1) {
            prev->unlock(true);
            return;
        }
        for(uint64_t i=0;i<length;i++){
            if(index>=prev->size){
                if(prev->next != nullptr) prev->next->lock(true);
                nodeType* t = prev->next;
                prev->unlock(true);
                prev = t;
                index = 0;
            }
            if(prev == nullptr) return;
            if(prev->data[index].key == nullptr) {
                prev->unlock(true);
                return;
            }
            func(*prev->data[index].key, *prev->data[index].val);
            index++;
        }
        prev->unlock(true);
    }

    ~SimpleSkiplist() {
        nodeType* cur = structure[MAX_HEIGHT-1];
        nodeType* down_next = cur->data[0].down;
        while(true){
            while(cur!=nullptr){
                nodeType* next = cur->next;
                delete cur;
                cur = next;
            }
            if(down_next!=nullptr){
                cur = down_next;
                down_next = cur->data[0].down;
            } else {
                break;
            }
        }
    }

    void print() {
        // first pass: assign each node a short ID
        std::map<nodeType*, int> ids;
        int next_id = 0;
        for (int i = MAX_HEIGHT - 1; i >= 0; i--) {
            nodeType* cur = structure[i];
            while (cur != nullptr) {
                if (ids.find(cur) == ids.end())
                    ids[cur] = next_id++;
                cur = cur->next;
            }
        }

        // second pass: print
        for (int i = MAX_HEIGHT - 1; i >= 0; i--) {
            std::cout << "Level " << i << ":";
            nodeType* cur = structure[i];
            while (cur != nullptr) {
                if (cur->size == 0) { cur = cur->next; continue; }
                std::cout << " → [N" << ids[cur] << " sz=" << (int)cur->size << "|";
                for (int j = 0; j < cur->size; j++) {
                    if (j > 0) std::cout << ",";
                    if (cur->data[j].key == nullptr) std::cout << "-∞";
                    else std::cout << *cur->data[j].key;
                    if (cur->data[j].down != nullptr)
                        std::cout << "↓N" << ids[cur->data[j].down];
                }
                std::cout << "]";
                cur = cur->next;
            }
            std::cout << " → [+∞]\n";
        }
        std::cout << "\n";
    }

};