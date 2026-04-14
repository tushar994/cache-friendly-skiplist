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
    std::vector<nodeType> structure;

    public:
    SimpleSkiplist() : structure(MAX_HEIGHT) {
        structure.reserve(MAX_HEIGHT);
        for(int i=0;i<MAX_HEIGHT;i++){
            if(i>0){
                structure[i].down = &structure[i-1];
            }
        }
        for(int i=0;i<MAX_HEIGHT;i++){
            nodeType* n = new nodeType();
            structure[i].next = n;
            if(i>0){
                n->down = structure[i-1].next;
            }
        }
    }

    std::optional<V> get(K key) {
        nodeType* cur = &structure[MAX_HEIGHT-1];
        while(true){
            if(cur == nullptr){
                return std::nullopt;
            }
            if(cur->next->next == nullptr || *cur->next->key > key){
                cur = cur->down;
            } else if (*cur->next->key == key) {
                cur = cur->next;
                while(cur->down != nullptr){
                    cur = cur->down;
                }
                return *cur->val;
            } else {
                cur = cur->next;
            }
        }
    }

    void insert_or_modify(K key, V val) {
        nodeType* cur = &structure[MAX_HEIGHT-1];
        int level = _gen(MAX_HEIGHT);

        nodeType* older_level_val = nullptr;
        int cur_height = MAX_HEIGHT-1;
        while(true){
            // std::cout<<"at this height: "<<cur_height<<"\n";
            if(cur->next->next == nullptr || *cur->next->key > key){
                // not found at this level.
                if(level>=cur_height){
                    nodeType* new_val = new nodeType();
                    new_val->next = cur->next;
                    cur->next = new_val;
                    new_val->key = new K(key);

                    if(older_level_val != nullptr){
                        older_level_val->down = new_val;
                    }
                    older_level_val = new_val;
                    if(cur_height == 0) {
                        new_val->val = new V(val);
                        break;
                    }
                }
                cur = cur->down;
                cur_height--;
            } else if (*cur->next->key == key) {
                if(older_level_val != nullptr){
                    older_level_val->down = cur->next;
                }
                cur = cur->next;
                while(cur->down != nullptr){
                    cur = cur->down;
                }
                cur->val = new V(val);
                break;
            } else {
                cur = cur->next;
            }
        }
    }

    bool remove(K key) {
        nodeType* cur = &structure[MAX_HEIGHT-1];
        while(true){
            if(cur == nullptr){
                return false;
            }
            if(cur->next->next == nullptr || *cur->next->key > key){
                cur = cur->down;
            } else if (*cur->next->key == key) {
                nodeType* prev = cur;
                cur = cur->next;
                while(cur != nullptr){
                    prev->next = cur->next;
                    nodeType* down = cur->down;
                    delete cur;
                    cur = down;
                    prev = prev->down;
                    if(cur == nullptr) break;
                    while(*prev->next->key != key){
                        prev = prev->next;
                    }
                }
                return true;
            } else {
                cur = cur->next;
            }
        }
    }

    template <typename F>
    void range(K key, F func, uint64_t length){
        nodeType* cur = &structure[MAX_HEIGHT-1];
        while(true){
            if(cur->next->next == nullptr || *cur->next->key > key){
                if(cur->down == nullptr) break;
                cur = cur->down;
            } else if (*cur->next->key == key) {
                cur = cur->next;
                while(cur->down != nullptr){
                    cur = cur->down;
                }
                break;
            } else {
                cur = cur->next;
            }
        }
        if(cur->key == nullptr) return;
        if(*cur->key < key) cur = cur->next;
        for(uint64_t i=0;i<length;i++){
            if(cur->key == nullptr) break;
            func(*cur->key, *cur->val);
            cur = cur->next;
        }
    }

    ~SimpleSkiplist() {
        nodeType* cur = &structure[MAX_HEIGHT-1];
        nodeType* down_next = cur->down;
        while(true){
            cur = cur->next;
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

};