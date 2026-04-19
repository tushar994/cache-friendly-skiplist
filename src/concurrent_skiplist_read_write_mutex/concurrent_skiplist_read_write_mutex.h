#ifndef _SIMPLE_SKIPLIST
#define _SIMPLE_SKIPLIST

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

#endif

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
struct SimpleSkiplistNode {
    V* val;
    K* key;
    SimpleSkiplistNode* next;
    SimpleSkiplistNode* down;
    std::shared_mutex _mut;

    SimpleSkiplistNode(K* k = nullptr, V* v = nullptr, SimpleSkiplistNode* n = nullptr, SimpleSkiplistNode* dw = nullptr) 
        : val(v), key(k), next(n), down(dw) 
    {}

    ~SimpleSkiplistNode() {
        if(val!=nullptr) delete val;
        if(key!=nullptr) delete key;
        next = nullptr;
        down = nullptr;
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
    using nodeType = SimpleSkiplistNode<K,V>;
    private:
    static_assert(MAX_HEIGHT > 1);
    LevelGen _gen;
    nodeType* structure[MAX_HEIGHT];

    template <typename Func, typename... Args>
    void move_down_prev_cur_locked(bool read_only_unlock, bool read_only_lock, nodeType*& cur, nodeType*& prev, Func&& func, Args&&... args){
        std::invoke(std::forward<Func>(func), cur, prev, std::forward<Args>(args)...);

        cur->unlock(read_only_unlock);

        nodeType* t = prev->down;
        if(t!=nullptr) t->lock(read_only_lock);
        prev->unlock(read_only_unlock);
        prev = t;
    }

    template <typename Func, typename... Args>
    void move_right_prev_cur_locked(bool read_only, nodeType*& cur, nodeType*& prev, Func&& func, Args&&... args){

        using ReturnType = std::invoke_result_t<Func, nodeType*&, nodeType*&, Args...>;

        std::invoke(std::forward<Func>(func), cur, prev, std::forward<Args>(args)...);

        prev->unlock(read_only);
        prev = cur;
    }

    public:
    SimpleSkiplist() {
        for(int i=0;i<MAX_HEIGHT;i++){
            structure[i] = new nodeType();
            if(i>0){
                structure[i]->down = structure[i-1];
            }
        }
        for(int i=0;i<MAX_HEIGHT;i++){
            nodeType* n = new nodeType();
            structure[i]->next = n;
            if(i>0){
                n->down = structure[i-1]->next;
            }
        }
    }

    std::optional<V> get(K key) {
        structure[MAX_HEIGHT-1]->lock(true);
        nodeType* prev = structure[MAX_HEIGHT-1];
        while(true){
            if(prev == nullptr || prev->next == nullptr){
                if(prev!=nullptr) prev->unlock(true);
                return std::nullopt;
            }
            prev->next->lock(true);
            nodeType* cur = prev->next;

            if(cur->next == nullptr || *cur->key > key){
                move_down_prev_cur_locked(true, true, cur, prev, [](nodeType*&, nodeType*&){});

            } else if (*cur->key == key) {
                prev->unlock(true);
                while(cur->down != nullptr){
                    cur->down->lock(true);
                    nodeType* d = cur->down;
                    cur->unlock(true);
                    cur = d;
                }
                V val = *cur->val;
                cur->unlock(true);
                return val;
            } else {
                move_right_prev_cur_locked(true, cur, prev, [](nodeType*&, nodeType*&){});
            }
        }
    }

    void insert_or_modify(K key, V val) {
        int level = _gen(MAX_HEIGHT);
        nodeType* older_level_val = nullptr;
        int cur_height = MAX_HEIGHT-1;
        bool read_only = true;
        if(level >= cur_height){
            read_only = false;
        }

        structure[MAX_HEIGHT-1]->lock(read_only);
        nodeType* prev = structure[MAX_HEIGHT-1];
        while(true){
            // std::cout<<"at this height: "<<cur_height<<"\n";
            if(prev==nullptr || prev->next == nullptr){
                if(prev!=nullptr) prev->unlock(read_only);
                return;
            }
            prev->next->lock(read_only);
            nodeType* cur = prev->next;
            if(cur->next == nullptr || *cur->key > key){
                // not found at this level.
                bool next_read_only = read_only;
                if(cur_height == (level + 1)) next_read_only = false;
                move_down_prev_cur_locked(read_only, next_read_only, cur,prev, [](nodeType*& cur, nodeType*& prev, int& cur_height, int& level, nodeType*& older_level_val, K& key, V& val, bool read_only){
                    if(level>=cur_height){
                        nodeType* new_val = new nodeType();
                        new_val->lock(read_only);

                        new_val->next = cur;
                        prev->next = new_val;
                        new_val->key = new K(key);

                        if(older_level_val != nullptr){
                            older_level_val->down = new_val;
                            older_level_val->unlock(read_only);
                        }
                        older_level_val = new_val;
                        if(cur_height == 0) {
                            new_val->val = new V(val);
                            new_val->unlock(read_only);
                        }
                    }
                }, cur_height, level, older_level_val, key, val, read_only);
                cur_height--;
                read_only = next_read_only;
            } else if (*cur->key == key) {
                prev->unlock(read_only);
                if(older_level_val != nullptr){
                    older_level_val->down = cur;
                    older_level_val->unlock(read_only);
                }
                while(cur->down != nullptr){
                    if(cur_height == 1) cur->down->lock(false);
                    else cur->down->lock(true);
                    nodeType* t = cur->down;
                    cur->unlock(read_only);
                    read_only = true;
                    cur = t;
                    cur_height--;
                }
                cur->val = new V(val);
                cur->unlock(false);
                break;
            } else {
                move_right_prev_cur_locked(read_only, cur, prev, [](nodeType*&, nodeType*&){});
            }
        }
    }

    bool remove(K key) {
        structure[MAX_HEIGHT-1]->lock(false);
        nodeType* prev = structure[MAX_HEIGHT-1];
        while(true){
            if(prev==nullptr || prev->next == nullptr){
                if(prev!=nullptr) prev->unlock(false);
                return false;
            }
            prev->next->lock(false);
            nodeType* cur = prev->next;

            if(cur->next == nullptr || *cur->key > key){
                move_down_prev_cur_locked(false, false, cur, prev, [](nodeType*&, nodeType*&){});
            } else if (*cur->key == key) {
                while(true){
                    while(*cur->key < key){
                        cur->next->lock(false);
                        nodeType* t = cur->next;
                        prev->unlock(false);
                        prev = cur;
                        cur = t;
                    }
                    prev->next = cur->next;
                    if(prev->down!=nullptr) prev->down->lock(false);
                    nodeType* down = prev->down;
                    delete cur;
                    prev->unlock(false);
                    if(down == nullptr) break;
                    down->next->lock(false);
                    cur = down->next;
                    prev = down;
                }
                return true;
            } else {
                move_right_prev_cur_locked(false, cur, prev, [](nodeType*&, nodeType*&){});
            }
        }
    }

    template <typename F>
    void range(K key, F func, uint64_t length){
        structure[MAX_HEIGHT-1]->lock(true);
        nodeType* prev = structure[MAX_HEIGHT-1];
        nodeType* cur;
        while(true){
            if(prev==nullptr || prev->next == nullptr){
                if(prev!=nullptr) prev->unlock(true);
                return;
            }
            prev->next->lock(true);
            cur = prev->next;

            if(cur->next == nullptr || *cur->key > key){
                if(cur->down == nullptr){
                    prev->unlock(true);
                    break;
                }
                move_down_prev_cur_locked(true, true, cur, prev, [](nodeType*&, nodeType*&){});
            } else if (*cur->key == key) {
                prev->unlock(true);
                while(cur->down != nullptr){
                    cur->down->lock(true);
                    nodeType* t = cur->down;
                    cur->unlock(true);
                    cur = t;
                }
                break;
            } else {
                move_right_prev_cur_locked(true, cur, prev, [](nodeType*&, nodeType*&){});
            }
        }
        if(cur->key == nullptr) {
            cur->unlock(true);
            return;
        }
        for(uint64_t i=0;i<length;i++){
            if(cur->key == nullptr) {
                cur->unlock(true);
                return;
            }
            func(*cur->key, *cur->val);
            cur->next->lock(true);
            nodeType* t = cur->next;
            cur->unlock(true);
            cur = t;
        }
        cur->unlock(true);
    }

    ~SimpleSkiplist() {
        nodeType* cur = structure[MAX_HEIGHT-1];
        nodeType* down_next = cur->down;
        while(true){
            while(cur!=nullptr){
                nodeType* next = cur->next;
                delete cur;
                cur = next;
            }
            if(down_next!=nullptr){
                cur = down_next;
                down_next = cur->down;
            } else {
                break;
            }
        }
    }

    void print() {
      for (int i = MAX_HEIGHT - 1; i >= 0; i--) {
          std::cout << "Level " << i << ": [-∞]";
          nodeType* cur = structure[i]->next;
          while (cur != nullptr && cur->key != nullptr) {
              std::cout << " → [" << *cur->key;
              if (cur->down != nullptr && cur->down->key != nullptr)
                  std::cout << " ↓" << *cur->down->key;
              else if (cur->down != nullptr && cur->down->key == nullptr)
                  std::cout << " ↓sentinel";
              else
                  std::cout << " ↓null";
              std::cout << "]";
              cur = cur->next;
          }
          std::cout << " → [+∞]\n";
      }
      std::cout << "\n";
  }

};