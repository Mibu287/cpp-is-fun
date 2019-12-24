#ifndef QUEUE_THREAD_SAFE
#define QUEUE_THREAD_SAFE
#include <atomic>
#include <memory>
#include <utility>

template<typename T>
class Queue_TS
{
public:
    //ctors, assignments, dtor
    Queue_TS(std::size_t __hazard_holder_size = 16,
             std::size_t __garbage_max_size = 1024);
    Queue_TS(Queue_TS const&) = delete;
    Queue_TS& operator=(Queue_TS const&) = delete;
    Queue_TS(Queue_TS&&) = delete;
    Queue_TS& operator=(Queue_TS&&) = delete;
    ~Queue_TS();

    //operations
    void push(T new_value);
    std::unique_ptr<T> pop();

private:
    //member types
    struct node;
    struct hazard;

    //helper functions for memory management
    node* load_head_set_hazard(std::size_t&);
    void clear_hazard(std::size_t);
    inline bool is_hazardous(node*);
    void push_to_garbage(node*);
    inline bool is_garbage_full();
    void collect_garbage();
    
    //member data
    std::atomic<node*> head;
    std::atomic<node*> tail;
    std::atomic<node*> garbage;
    std::atomic_size_t garbage_size;
    std::size_t garbage_max_size;
    hazard* hazard_holder;
    std::size_t hazard_holder_size;
};

template<typename T>
struct Queue_TS<T>::node
{
    //ctors, assignements, dtor
    node(): taken ATOMIC_FLAG_INIT, data{}, next{} {} ;
    node(node const&) = delete;
    node operator=(node const&) = delete;
    node(node&&) = delete;
    node operator=(node&&) = delete;

    ~node()
    {
        if(next)
            delete next;

        T* old_data = data.load();
        data.store(nullptr);
        if(old_data)
            delete old_data;
    }

    //member data
    std::atomic_flag taken;
    std::atomic<T*> data;
    node* next;
};

template<typename T>
struct Queue_TS<T>::hazard
{
    //ctors, assignments, dtor
    hazard(): taken ATOMIC_FLAG_INIT, pointer{} {};
    hazard(hazard const&) = delete;
    hazard& operator=(hazard const&) = delete;
    hazard(hazard&&) = delete;
    hazard& operator=(hazard&&) = delete;
    ~hazard() = default;

    //member data
    std::atomic_flag taken;
    std::atomic<node*> pointer;
};

template<typename T>
Queue_TS<T>::Queue_TS(std::size_t __hazard_holder_size,
                      std::size_t __garbage_max_size)
        :head{}, tail{}, garbage{}, hazard_holder{}
{
    node* new_node = new node{};
    head.store(new_node);
    tail.store(new_node);
    
    hazard_holder_size = __hazard_holder_size;
    hazard_holder = new hazard[hazard_holder_size] {};

    garbage_size.store(0ul);
    garbage_max_size = __garbage_max_size;
}

template<typename T>
Queue_TS<T>::~Queue_TS()
{
    //linked nodes will be deconstructed recursively
    //when each previous node is deconstructed

    delete head.load(); //head is never be nullptr
    
    node* __garbage = garbage.load();
    if(__garbage)
        delete __garbage;
}

template<typename T>
void Queue_TS<T>::push(T new_value)
{
    T* new_data = new T{std::move(new_value)};
    node* new_node = new node{};
    node* old_tail;
    T* dummy_ptr;

    do
    {
        dummy_ptr = nullptr;
        old_tail = tail.load();
    }
    while(!old_tail->data.compare_exchange_strong(dummy_ptr, new_data));
    
    old_tail->next = new_node;
    tail.store(new_node);
}

template<typename T>
typename Queue_TS<T>::node* Queue_TS<T>::load_head_set_hazard(std::size_t& slot)
{
    slot = 0;

    //keep looking until a free slot is found in hazard holder
    while(true){
        bool taken = hazard_holder[slot].taken.test_and_set();
        
        if(!taken)
            break;

        slot = ++slot % hazard_holder_size;
    }

    using node = typename Queue_TS<T>::node;
    node* old_head;

    do
    {
        old_head = head.load();
        hazard_holder[slot].pointer.store(old_head);    
    }
    while(old_head != head.load());

    return old_head;
}

template<typename T>
void Queue_TS<T>::clear_hazard(std::size_t slot)
{
    hazard_holder[slot].pointer.store(nullptr);
    hazard_holder[slot].taken.clear();
}

template<typename T>
inline bool Queue_TS<T>::is_hazardous(typename Queue_TS<T>::node* disposable)
{
    for(std::size_t i = 0; i < hazard_holder_size; ++i)
        if(hazard_holder[i].pointer.load() == disposable)
            return true;

    return false;
}

template<typename T>
void Queue_TS<T>::push_to_garbage(typename Queue_TS<T>::node* disposable)
{
    disposable->next = garbage.load();
    while(!garbage.compare_exchange_weak(disposable->next, disposable))
        ;//Empty loop body

    ++garbage_size;
}

template<typename T>
inline bool Queue_TS<T>::is_garbage_full()
{
    if(garbage_size.load() < garbage_max_size)
        return false;
    return true;
}

template<typename T>
void Queue_TS<T>::collect_garbage()
{
    using node = typename Queue_TS<T>::node;
    node* __garbage = garbage.exchange(nullptr);

    while(__garbage){
        --garbage_size;
        node* tmp = __garbage->next;
        __garbage->next = nullptr;

        if(is_hazardous(__garbage))
            push_to_garbage(__garbage);
        else
           delete __garbage;
            
        __garbage = tmp;
    }
}

template<typename T>
std::unique_ptr<T> Queue_TS<T>::pop()
{
    node* old_head;
    std::size_t slot = 0;

    //only thread which set taken flag can pop head
    while(true){
        old_head = load_head_set_hazard(slot);
        bool taken = old_head->taken.test_and_set();

        //clear hazard right after test_and_set on taken flag
        //because if test_and_set fail ==> no need to dereference old_head
        //if successful, taken flag is set, no other thread can delete old_head
        clear_hazard(slot);

        if(!taken)
            break;
    }

    //case 1: head is dummy node, not pop head
    // return an empty smart pointer
    if(old_head == tail.load()){
        old_head->taken.clear();
        return std::unique_ptr<T>{};
    }

    //case 2: head is not dummy ==> pop head
    //return data and do garbage collection
    head.store(old_head->next);
    T* old_data = old_head->data.load();
    std::unique_ptr<T> res {old_data};

    //collect garbage 
    old_head->data.store(nullptr);
    old_head->next = nullptr;

    if(is_hazardous(old_head))
        push_to_garbage(old_head);
    else
        delete old_head;

    if(is_garbage_full())
        collect_garbage();

    return res;
}

#endif //QUEUE_THREAD_SAFE
