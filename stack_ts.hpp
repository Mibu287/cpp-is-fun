#ifndef STACK_THREAD_SAFE_H
#define STACK_THREAD_SAFE_H
#include <atomic>
#include <thread>
#include <memory>

template<typename T>
class Stack_TS
{
public:
    //ctors, asssigments and dtor
    Stack_TS(std::size_t _max_size = 32);
    Stack_TS(Stack_TS const&) = delete;
    Stack_TS& operator=(Stack_TS const&) = delete;
    Stack_TS(Stack_TS&&);
    Stack_TS& operator=(Stack_TS&&);
    ~Stack_TS();

    //operations
    void push(T data);
    std::shared_ptr<T> pop();
    bool is_empty();
    
private:
    //member types
    struct node;
    struct hazard_ptr;
    
    //helper
    hazard_ptr *get_hazard_pointer();
    node* set_hazard_pointer(hazard_ptr*);
    void clear_hazard_pointer(hazard_ptr*);
    bool hazard_existed(node*);
    void claim_later(node*);
    void collect_garbage(void);

    //member data
    std::atomic<node*> head;
    std::atomic<node*> garbage;
    hazard_ptr *hazard_arr;
    std::size_t hazard_max_size;
};

template<typename T>
Stack_TS<T>::Stack_TS(std::size_t _max_size): hazard_max_size{_max_size}, head{}, garbage{}
{
    hazard_arr = new hazard_ptr[hazard_max_size] {};
}

template<typename T>
Stack_TS<T>::Stack_TS(Stack_TS<T>&& other)
{
    hazard_max_size = other.hazard_max_size;
    hazard_arr = other.hazard_arr;
    other.hazard_arr = nullptr;

    node* _head_tmp = other.head.exchange(nullptr);
    head.store(_head_tmp);

    node* _garbage_tmp = other.garbage.exchange(nullptr);
    garbage.store(_head_tmp);
}

template<typename T>
Stack_TS<T>& Stack_TS<T>::operator=(Stack_TS<T>&& other)
{
    if(this->hazard_max_size > 0)
        throw "Can not assign to an intialized stack";

    hazard_max_size = other.hazard_max_size;
    hazard_arr = other.hazard_arr;
    other.hazard_arr = nullptr;

    node* _head_tmp = other.head.exchange(nullptr);
    head.store(_head_tmp);

    node* _garbage_tmp = other.garbage.exchange(nullptr);
    garbage.store(_head_tmp);
}

template<typename T>
Stack_TS<T>::~Stack_TS()
{
    //clean stack
    node* head_ptr = head.exchange(nullptr);
    if(head_ptr)
        delete head_ptr;
    
    //clean garbage
    node* garbage_ptr = garbage.exchange(nullptr);
    if(garbage_ptr)
        delete garbage_ptr;

    //clean hazard list
    delete[] hazard_arr;
}

template<typename T>
struct Stack_TS<T>::node
{
    //ctors, assignements, dtor
    node(T arg): next{}, data{std::make_shared<long>(std::move(arg))} {};

    node(node const&) = delete;
    node& operator=(node const&) = delete;
    
    node(node&& other): data{}, next{}
    {
        data.swap(other.data);
    }
    
    node& operator=(node&& rhs)
    {
        this->data.swap(rhs.data);
    }
    
    ~node()
    {
        if(next) 
            delete next;
    }
    
    //member data
    node* next;
    std::shared_ptr<T> data;
};

template<typename T>
struct Stack_TS<T>::hazard_ptr
{
    std::atomic<std::thread::id> thread_id;
    std::atomic<node*> pointer;
};

template<typename T>
void Stack_TS<T>::push(T arg)
{
    node* new_node = new node{std::move(arg)};
    new_node->next = head.load();

    while(!head.compare_exchange_weak(new_node->next, new_node))
        ;//Empty loop body
}

template<typename T>
typename Stack_TS<T>::hazard_ptr* Stack_TS<T>::get_hazard_pointer()
{
    hazard_ptr* result = nullptr;
    std::thread::id this_thread_id = std::this_thread::get_id();

    for(std::size_t idx = 0; idx < hazard_max_size; ++idx){
        std::thread::id default_id{};
        std::atomic<std::thread::id>& hazard_thread_id = hazard_arr[idx].thread_id;

        bool get_slot = hazard_thread_id.compare_exchange_strong(default_id, this_thread_id);
        if(get_slot)
            return &hazard_arr[idx];
    } 

    return result;
}

template<typename T>
typename Stack_TS<T>::node* Stack_TS<T>::set_hazard_pointer(hazard_ptr *hp)
{
    node* old_head = head.load();
    hp->pointer.store(old_head);
    return old_head;
}

template<typename T>
void Stack_TS<T>::clear_hazard_pointer(hazard_ptr *hp)
{
    hp->pointer.store(nullptr);
    hp->thread_id.store(std::thread::id{});
}

template<typename T>
bool Stack_TS<T>::hazard_existed(typename Stack_TS<T>::node *disposable)
{
    for(std::size_t idx = 0; idx < hazard_max_size; ++idx)
        if(hazard_arr[idx].pointer.load() == disposable)
            return true;

    return false;
}

template<typename T>
void Stack_TS<T>::claim_later(typename Stack_TS<T>::node* disposable)
{
    disposable->next = garbage.load();

    while(!garbage.compare_exchange_weak(disposable->next, disposable))
        ;//Empty loop body
}

template<typename T>
void Stack_TS<T>::collect_garbage()
{
    node *claimed_garbage = garbage.exchange(nullptr);

    while(claimed_garbage){
        node *tmp = claimed_garbage->next;
        claimed_garbage->next = nullptr;

        if(hazard_existed(claimed_garbage))
            claim_later(claimed_garbage);
        else
            delete claimed_garbage;

        claimed_garbage = tmp;
    }
}

template<typename T>
std::shared_ptr<T> Stack_TS<T>::pop()
{
    hazard_ptr *hp = nullptr;
    while(!hp)
        hp = get_hazard_pointer();

    node* old_head;
        
    do
    {
        old_head = set_hazard_pointer(hp);
    }
    while(old_head && !head.compare_exchange_strong(old_head, old_head->next));
 
    clear_hazard_pointer(hp);   

    if(!old_head)
        return std::shared_ptr<T>{};
    
    std::shared_ptr<T> res;
    res.swap(old_head->data);   
    old_head->next = nullptr;

    if(!hazard_existed(old_head))
        delete old_head;
    else
        claim_later(old_head);

    collect_garbage();

    return res;
}

template<typename T>
bool Stack_TS<T>::is_empty()
{
    if(head.load() == nullptr)
        return true;
    return false;
}
#endif //STACK_THREAD_SAFE_H
