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

constexpr int num_tries = 3;
class Lock {
    std::atomic<bool> flag{false};

    public:
    Lock(){}

    ~Lock(){}

    void unlock(){
        flag.exchange(false);
    }

    void lock(){
        int tries = 0;
        while(true){
            bool f = false;
            bool success = flag.compare_exchange_weak(f, true);
            if(success){
                return;
            } else {
                tries++;
            }

            if(tries >= num_tries) {
                tries = 0;
                std::this_thread::yield();
            }
        }
    }

    bool try_lock(){
        bool f = false;
        bool success = flag.compare_exchange_strong(f,true);
        return success;
    }

};

template <typename K, typename V>
struct SimpleSkiplistNode {
    V* val;
    K* key;
    SimpleSkiplistNode* next;
    SimpleSkiplistNode* down;
    Lock lock;

    SimpleSkiplistNode(K* k = nullptr, V* v = nullptr, SimpleSkiplistNode* n = nullptr, SimpleSkiplistNode* dw = nullptr) 
        : val(v), key(k), next(n), down(dw) 
    {}

    ~SimpleSkiplistNode() {
        if(val!=nullptr) delete val;
        if(key!=nullptr) delete key;
        next = nullptr;
        down = nullptr;
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
    void move_down_prev_cur_locked(nodeType*& cur, nodeType*& prev, Func&& func, Args&&... args){
        using ReturnType = std::invoke_result_t<Func, nodeType*&, nodeType*&, Args...>;

        std::invoke(std::forward<Func>(func), cur, prev, std::forward<Args>(args)...);

        cur->lock.unlock();
        nodeType* t = prev->down;
        if(t!=nullptr) t->lock.lock();
        prev->lock.unlock();
        prev = t;
    }

    template <typename Func, typename... Args>
    void move_right_prev_cur_locked(nodeType*& cur, nodeType*& prev, Func&& func, Args&&... args){

        using ReturnType = std::invoke_result_t<Func, nodeType*&, nodeType*&, Args...>;

        std::invoke(std::forward<Func>(func), cur, prev, std::forward<Args>(args)...);

        prev->lock.unlock();
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
        structure[MAX_HEIGHT-1]->lock.lock();
        nodeType* prev = structure[MAX_HEIGHT-1];
        while(true){
            if(prev == nullptr || prev->next == nullptr){
                if(prev!=nullptr) prev->lock.unlock();
                return std::nullopt;
            }
            prev->next->lock.lock();
            nodeType* cur = prev->next;

            if(cur->next == nullptr || *cur->key > key){
                move_down_prev_cur_locked(cur, prev, [](nodeType*&, nodeType*&){});

            } else if (*cur->key == key) {
                prev->lock.unlock();
                while(cur->down != nullptr){
                    nodeType* d = cur->down;
                    cur->lock.unlock();
                    cur = d;
                    cur->lock.lock();
                }
                V val = *cur->val;
                cur->lock.unlock();
                return val;
            } else {
                move_right_prev_cur_locked(cur, prev, [](nodeType*&, nodeType*&){});
            }
        }
    }

    void insert_or_modify(K key, V val) {
        int level = _gen(MAX_HEIGHT);
        nodeType* older_level_val = nullptr;
        int cur_height = MAX_HEIGHT-1;

        structure[MAX_HEIGHT-1]->lock.lock();
        nodeType* prev = structure[MAX_HEIGHT-1];
        while(true){
            // std::cout<<"at this height: "<<cur_height<<"\n";
            if(prev==nullptr || prev->next == nullptr){
                if(prev!=nullptr) prev->lock.unlock();
                return;
            }
            prev->next->lock.lock();
            nodeType* cur = prev->next;
            if(cur->next == nullptr || *cur->key > key){
                // not found at this level.
                move_down_prev_cur_locked(cur,prev, [](nodeType*& cur, nodeType*& prev, int& cur_height, int& level, nodeType*& older_level_val, K& key, V& val){
                    if(level>=cur_height){
                        nodeType* new_val = new nodeType();
                        new_val->lock.lock();

                        new_val->next = cur;
                        prev->next = new_val;
                        new_val->key = new K(key);

                        if(older_level_val != nullptr){
                            older_level_val->down = new_val;
                            older_level_val->lock.unlock();
                        }
                        older_level_val = new_val;
                        if(cur_height == 0) {
                            new_val->val = new V(val);
                            new_val->lock.unlock();
                        }
                    }
                }, cur_height, level, older_level_val, key, val);
                cur_height--;
            } else if (*cur->key == key) {
                prev->lock.unlock();
                if(older_level_val != nullptr){
                    older_level_val->down = cur;
                    older_level_val->lock.unlock();
                }
                while(cur->down != nullptr){
                    cur->down->lock.lock();
                    nodeType* t = cur->down;
                    cur->lock.unlock();
                    cur = t;
                }
                cur->val = new V(val);
                cur->lock.unlock();
                break;
            } else {
                move_right_prev_cur_locked(cur, prev, [](nodeType*&, nodeType*&){});
            }
        }
    }

    bool remove(K key) {
        structure[MAX_HEIGHT-1]->lock.lock();
        nodeType* prev = structure[MAX_HEIGHT-1];
        while(true){
            if(prev==nullptr || prev->next == nullptr){
                if(prev!=nullptr) prev->lock.unlock();
                return false;
            }
            prev->next->lock.lock();
            nodeType* cur = prev->next;

            if(cur->next == nullptr || *cur->key > key){
                move_down_prev_cur_locked(cur, prev, [](nodeType*&, nodeType*&){});
            } else if (*cur->key == key) {
                while(true){
                    while(*cur->key < key){
                        cur->next->lock.lock();
                        nodeType* t = cur->next;
                        prev->lock.unlock();
                        prev = cur;
                        cur = t;
                    }
                    prev->next = cur->next;
                    if(prev->down!=nullptr) prev->down->lock.lock();
                    nodeType* down = prev->down;
                    delete cur;
                    prev->lock.unlock();
                    if(down == nullptr) break;
                    down->next->lock.lock();
                    cur = down->next;
                    prev = down;
                }
                return true;
            } else {
                move_right_prev_cur_locked(cur, prev, [](nodeType*&, nodeType*&){});
            }
        }
    }

    template <typename F>
    void range(K key, F func, uint64_t length){
        structure[MAX_HEIGHT-1]->lock.lock();
        nodeType* prev = structure[MAX_HEIGHT-1];
        nodeType* cur;
        while(true){
            if(prev==nullptr || prev->next == nullptr){
                if(prev!=nullptr) prev->lock.unlock();
                return;
            }
            prev->next->lock.lock();
            cur = prev->next;

            if(cur->next == nullptr || *cur->key > key){
                if(cur->down == nullptr){
                    prev->lock.unlock();
                    break;
                }
                move_down_prev_cur_locked(cur, prev, [](nodeType*&, nodeType*&){});
            } else if (*cur->key == key) {
                prev->lock.unlock();
                while(cur->down != nullptr){
                    cur->down->lock.lock();
                    nodeType* t = cur->down;
                    cur->lock.unlock();
                    cur = t;
                }
                break;
            } else {
                move_right_prev_cur_locked(cur, prev, [](nodeType*&, nodeType*&){});
            }
        }
        if(cur->key == nullptr) {
            cur->lock.unlock();
            return;
        }
        for(uint64_t i=0;i<length;i++){
            if(cur->key == nullptr) {
                cur->lock.unlock();
                return;
            }
            func(*cur->key, *cur->val);
            cur = cur->next;
        }
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