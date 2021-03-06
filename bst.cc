#ifndef BST_H_
#include "bst.h"
#endif

#include <sys/mman.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

BSTree::BSTree(std::string po_name) {
    po_name_ = po_name;
    pod_ = -1;
    meta_ = NULL;
}

int BSTree::Open() {
    /* just the owner has read and write privilege */
    pod_ = po_open(po_name_.c_str(), O_CREAT|O_RDWR, S_IRUSR|S_IWUSR);
    if(pod_==-1) {
        std::cout<<"open po failed, errno: "<<errno<<std::endl;
        return -1;
    }
    /* get po state */
    struct po_stat statbuf;
    if(po_fstat(pod_, &statbuf) == -1) {
        std::cout<<"get po state failed, errno: "<<errno<<std::endl;
        return -1;
    }
    /* map po */
    if(statbuf.st_size != 0) {
        unsigned long chunks[1];
        chunks[0] = (unsigned long)NULL;
        bool is_first_chunk = true;
        do {
            if (po_chunk_next(pod_, chunks[0], 1, chunks) < 0) {
                std::cout << "po chunk next error, errno: " << errno << std::endl;
                return -1;
            }
            if (chunks[0] == (unsigned long)NULL)
                break;
            if (is_first_chunk) {
                meta_ = (BSTreeMetadata*)(chunks[0]);
                is_first_chunk = false;
            }
            if (po_chunk_mmap(pod_, chunks[0], PROT_READ|PROT_WRITE, MAP_PRIVATE) < 0) {
                std::cout << "po chunk mmap error, errno: " << errno << std::endl;
                return -1;
            }
        } while(true);
    }else {
        /* the first is used to store metadata */
        meta_ = (BSTreeMetadata *)po_extend(pod_, PAGE_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE);
        if(meta_ == NULL) {
            std::cout<<"malloc from po failed, errno: "<<errno<<std::endl;
            return -1;
        }
        meta_->root_node_ = NULL;
        meta_->size_ = 0;
#ifdef USE_SLAB
        /* init slab */
        po_memory_alloc_init(&meta_->s, sizeof(struct BSTreeNode));
        meta_->s.pod = pod_;
#endif
    }
    return 0;
}

int BSTree::Insert(Value_t val) {
    if(meta_ == NULL) {
        return -1;
    }

    BSTreeNode *tmp;
#ifdef USE_SLAB
    tmp = (BSTreeNode *)po_malloc(&meta_->s);
#else
    tmp = (BSTreeNode*)po_malloc(pod_, sizeof(struct BSTreeNode));
#endif
    if(tmp == NULL) {
        std::cout<<"malloc from po failed, errno: "<<errno<<std::endl;
        return -1;
    }
    tmp->value_ = val;
    tmp->left_ = NULL;
    tmp->right_ = NULL;
    tmp->parent_ = NULL;

    if(meta_->root_node_ == NULL) {
        meta_->root_node_ = tmp;
        meta_->size_++;
        return 0;
    }
    BSTreeNode *curr, *parent;
    curr = parent = meta_->root_node_;
    while(curr != NULL) {
        parent = curr;
        if(DefaultCmp(curr->value_, val) >= 0) {
            curr = curr->left_;
        }else {
            curr = curr->right_;
        }
    }
    tmp->parent_ = parent;
    if(DefaultCmp(parent->value_, val) >= 0) {
        parent->left_ = tmp;
    }else {
        parent->right_ = tmp;
    }
    meta_->size_++;
    return 0;
}



bool BSTree::Delete(Value_t val) {
    BSTreeNode *found_node = Find(val);
    if(found_node == NULL) {
        return false;
    }
    if(found_node->left_ == NULL) {
        Transplant(found_node, found_node->right_);
    }else if(found_node->right_ == NULL) {
        Transplant(found_node, found_node->left_);
    }else {
        BSTreeNode *successor = Minimum((found_node->right_));
        if(found_node->right_ != successor) {
            Transplant(successor, successor->right_);
            successor->right_ = found_node->right_;
            successor->right_->parent_ = successor;
        }
        Transplant(found_node, successor);
        successor->left_ = found_node->left_;
        successor->left_->parent_ = successor;
    }
    meta_->size_--;
#ifdef USE_SLAB
    po_free(&meta_->s, found_node);
#else
    po_free(pod_, found_node);
#endif
    return true;
}

bool BSTree::Search(Value_t val) {
    BSTreeNode *found_node = Find(val);
    if (found_node == NULL)
        return false;
    else
        return true; 
}


BSTreeNode* BSTree::Find(Value_t val) {
    if (meta_ == NULL) {
        return NULL;
    }
    if (meta_->root_node_ == NULL) {
        return NULL;
    }
    BSTreeNode *curr = meta_->root_node_;
    int cmp_result;
    while(curr != NULL) {
        cmp_result = DefaultCmp(curr->value_, val);
        if(cmp_result == 0) {
            return curr;
        }else if(cmp_result > 0) {
            curr = curr->left_;
        }else {
            curr = curr->right_;
        }
    }
    return NULL;
}

BSTreeNode* BSTree::Minimum(BSTreeNode *curr) {
    if(curr == NULL) {
        return NULL;
    }
    while(curr->left_!=NULL) {
        curr = curr->left_;
    }
    return curr;
}

void BSTree::Transplant(BSTreeNode *old_node, BSTreeNode *new_node) {
    if(old_node->parent_ == NULL) {
        meta_->root_node_ = new_node;
    }else if(old_node->parent_->left_ == old_node) {
        old_node->parent_->left_ = new_node;
    }else {
        old_node->parent_->right_ = new_node;
    }
    if(new_node != NULL) {
        new_node->parent_ = old_node->parent_;
    }
}

int BSTree::DefaultCmp(Value_t val1, Value_t val2) {
    if(val1 == val2) {
        return 0;
    }else if(val1 > val2) {
        return 1;
    }else {
        return -1;
    }
}

int BSTree::Size() {
    if (meta_ != nullptr)
        return meta_->size_;
    return -1;
}
