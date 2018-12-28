// Copyright (c) 2018 The InSDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
//

#ifndef INSDB_DB_LIST_H_
#define INSDB_DB_LIST_H_

#include "insdb/status.h"
#include "db/insdb_internal.h"

namespace insdb{

    // C++ version of container_of
    template<class P, class M>
    size_t class_offsetof(const M P::*member)
    {
        return (size_t) &( reinterpret_cast<P*>(0)->*member);
    };

    template<class P, class M>
    P* class_container_of_impl(M* ptr, const M P::*member)
    {
        return (P*)( (char*)ptr - class_offsetof(member));
    };

#define class_container_of(ptr, type, member) \
     class_container_of_impl (ptr, &type::member)

    // Simple double linked list
    struct SimpleLinkedNode
    {
        SimpleLinkedNode() : next_(nullptr), prev_(nullptr) {}
        SimpleLinkedNode * next_;
        SimpleLinkedNode * prev_;
    };

    template<class LOCKTYPE>
    class SimpleLinkedList {
        public:
            explicit SimpleLinkedList() : list_(), lock_(), size_(0), threshold_(0) {
                list_.prev_ = &list_;
                list_.next_ = &list_;
            }

            //const SimpleLinkedNode* end() const { return &list_; }
            bool empty() const { return list_.next_ == &list_; }
            SimpleLinkedNode* oldest() const { return list_.prev_ == &list_ ? NULL: list_.prev_; }
            SimpleLinkedNode* newest() const { return list_.next_ == &list_ ? NULL: list_.next_; }
            SimpleLinkedNode* next(SimpleLinkedNode *cache) const { return cache->next_ == &list_ ? NULL: cache->next_; }
            SimpleLinkedNode* prev(SimpleLinkedNode *cache) const { return cache->prev_ == &list_ ? NULL: cache->prev_; }
            size_t Size(){ return size_; }
            void SetThreshold(size_t threshold) {
                threshold_= threshold;
            }
#if 0
            void IncreaseThreshold(size_t val) {
                threshold_+=val;
            }
#endif
            size_t GetThreshold(void) {
                return threshold_;
            }

            //void __attribute__((optimize("O0"))) InsertFront(SimpleLinkedNode *cache, size_t size = 1) {
            void InsertFront(SimpleLinkedNode *cache, size_t size = 1) {
                cache->prev_ = &list_;
                cache->next_ = list_.next_;
                cache->prev_->next_ = cache;
                cache->next_->prev_ = cache;
                size_+=size;
            }

            void InsertBack(SimpleLinkedNode *cache, size_t size = 1) {
                cache->next_ = &list_;
                cache->prev_ = list_.prev_;
                cache->prev_->next_ = cache;
                cache->next_->prev_ = cache;
                size_+=size;
            }

            void InsertToNext(SimpleLinkedNode *cache, SimpleLinkedNode *base, size_t size = 1) {
                cache->prev_ = base;
                cache->next_ = base->next_;
                cache->prev_->next_ = cache;
                cache->next_->prev_ = cache;
                size_+=size;
            }

            void Delete(SimpleLinkedNode *cache, SimpleLinkedNode *poison = nullptr, size_t size = 1) {
                cache->prev_->next_ = cache->next_;
                cache->next_->prev_ = cache->prev_;
                cache->next_ = cache->prev_ = poison;
                size_-=size;
            }

            /* Return olest */
            SimpleLinkedNode* Pop(SimpleLinkedNode *poison = nullptr, size_t size = 1) {
                if(list_.prev_ == &list_)
                    return NULL;
                SimpleLinkedNode *node = list_.prev_; 
                node->prev_->next_ = node->next_;
                node->next_->prev_ = node->prev_;
                node->next_ = node->prev_ = poison;
                size_-=size;
                return node; 
            }

            void MoveToFront(SimpleLinkedNode *cache) {
                /* Delete */
                cache->prev_->next_ = cache->next_;
                cache->next_->prev_ = cache->prev_;

                /* Insert to Front*/
                cache->prev_ = &list_;
                cache->next_ = list_.next_;
                cache->prev_->next_ = cache;
                cache->next_->prev_ = cache;
            }

            void Lock(){ lock_.Lock(); }
            bool TryLock(){ return lock_.TryLock(); }
            void Unlock(){ lock_.Unlock(); }

        private:
            SimpleLinkedNode list_;
            LOCKTYPE lock_;
            size_t size_;
            size_t threshold_;
    };


}  // namespace leveldb

#endif  // INSDB_DB_LIST_H_
