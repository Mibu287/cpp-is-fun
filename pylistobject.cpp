#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <vector>
#include <exception>

/* Python List Object reimplemented in C++
 * Support sequence and iterator protocols
 *
 * Notes:
 * Container of ListObject is C++ vector 
 * getitem and insert item at last position are fast
 * insert or delete item at arbitrary index are relatively slow
 */

class ListObject : public PyObject
{
public: /* Public interfaces */
    ListObject();
    ~ListObject();

    Py_ssize_t getlength() const {return this->container.size();};
    std::tuple<int, PyObject*> getter(Py_ssize_t index);
    std::tuple<int, PyObject*> setter(Py_ssize_t index, PyObject *value);
    std::tuple<int, PyObject*> deleter(Py_ssize_t index);

    friend PyObject *list_insert(PyObject *self, PyObject *args);

private: /* Data members */
    std::vector<PyObject*> container;
};

/* Sequence protocol */
Py_ssize_t list_length(PyObject *self);
PyObject *list_getitem(PyObject *self, Py_ssize_t index);
int list_setitem(PyObject *self, Py_ssize_t index, PyObject *value);

static PySequenceMethods list_sequence = {
    .sq_length = list_length,
    .sq_item = list_getitem,
    .sq_ass_item = list_setitem,
};

PyObject *list_new(PyTypeObject *type, PyObject *args, PyObject *kwargs);
void list_dealloc(PyObject *self);
PyObject *list_insert(PyObject *self, PyObject *args);

static PyMethodDef list_methods[] = {
    {"insert", list_insert, METH_VARARGS, nullptr},
    {nullptr, nullptr, 0, nullptr},
};

/* Iterator protocol */
PyObject *list_iter(PyObject *self);
PyObject *list_iternext(PyObject *self);

struct ListIterObject : public PyObject
{
    ListIterObject(ListObject *_list);
    ~ListIterObject();

    Py_ssize_t currentpos;
    ListObject *list; 
};

void listiter_dealloc(PyObject *self);

static PyTypeObject ListIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "ListIter",
    .tp_doc = "Iteration type of list object",
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = nullptr,
    .tp_dealloc = listiter_dealloc,
    .tp_basicsize = sizeof(ListIterObject),
    .tp_itemsize = 0,
    .tp_iter = &list_iter,
    .tp_iternext = &list_iternext,
};


ListIterObject::ListIterObject(ListObject *_list)
        : PyObject{0, &ListIterType},
          currentpos{0}, list{_list}
{
    Py_INCREF(this->list);
}


ListIterObject::~ListIterObject()
{
    assert(this->list == nullptr 
            && "ListIterObject is not cleared before deconstructed"); 
}


void
listiter_dealloc(PyObject *self)
{
    ListIterObject *_self = static_cast<ListIterObject*>(self);
    Py_CLEAR(_self->list);
    delete _self;
}


static PyTypeObject ListType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "List",
    .tp_doc = "List object reimplemented in C++",
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_basicsize = sizeof(ListObject),
    .tp_itemsize = 0,
    .tp_new = list_new,
    .tp_dealloc = list_dealloc,
    .tp_as_sequence = &list_sequence,
    .tp_methods = list_methods,
    .tp_iter = list_iter,
};


ListObject::ListObject()
        : PyObject{0, &ListType}, container{}
{/* Empty body */}


ListObject::~ListObject()
{
    for(PyObject*& obj : this->container)
        Py_CLEAR(obj);
}


PyObject *list_new(PyTypeObject *type, PyObject*, PyObject*)
{

    ListObject *listobj = nullptr;

    try
    {
        listobj = new ListObject{};
    }
    catch (std::bad_alloc)
    {
        PyErr_NoMemory();
        return NULL;
    }

    Py_INCREF(listobj);
    return listobj;
}


void 
list_dealloc(PyObject *self)
{
    ListObject *_self = static_cast<ListObject*>(self);
    delete _self; 
}


Py_ssize_t
list_length(PyObject *self)
{
    ListObject *_self = static_cast<ListObject*>(self);
    return _self->getlength();
}


enum status
{
    SUCCESS,
    ERROR,
};


PyObject*
list_getitem(PyObject *self, Py_ssize_t index)
{
    ListObject *_self = static_cast<ListObject*>(self);
    int status = 0;
    PyObject *value = nullptr;

    std::tie(status, value) = _self->getter(index); 
    
    if(status == ERROR){
        PyErr_SetNone(value);
        return NULL;
    }

    return value;
}


int
list_setitem(PyObject *self, Py_ssize_t index, PyObject *value)
{
    ListObject *_self = static_cast<ListObject*>(self);

    int status = 0;
    PyObject *error= nullptr;

    std::tie(status, error) = value ? _self->setter(index, value)
                                    : _self->deleter(index);
   
    if(status == ERROR){
        PyErr_SetNone(error); 
        return -1;
    }

    return 0;
}


PyObject*
list_insert(PyObject *self, PyObject *args)
{
    ListObject *_self = static_cast<ListObject*>(self);

    /* Parse and check arguments */
    PyObject *value = nullptr;
    Py_ssize_t index = -1;
    
    if(!PyArg_ParseTuple(args, "O|l", &value, &index)){
        PyErr_SetString(PyExc_TypeError, "Index must be of type integer");
        return nullptr;
    }

    Py_ssize_t last_index = _self->getlength();
    if(index == -1)
        index = last_index;

    if(index < 0 || index > last_index){
        PyErr_SetNone(PyExc_IndexError);
        return nullptr;
    }

    /* Insert a place holder into container */
    try
    {
        _self->container.push_back(nullptr);
    }
    catch (std::bad_alloc)
    {
        PyErr_NoMemory();
        return nullptr;
    }
    
    /* shift list items to make room for new item */
    for(Py_ssize_t i = last_index; i > index; --i){
        _self->container[i] = _self->container[i - 1];
    }

    Py_INCREF(value);
    _self->container[index] = value;
    Py_RETURN_NONE;
}

/* Implemetation of iterator protocol */
PyObject*
list_iter(PyObject *self)
{
    if(Py_TYPE(self) == &ListIterType)
        return self;

    ListObject *_self = static_cast<ListObject*>(self);
    ListIterObject *iterobj = new ListIterObject{_self};
    Py_INCREF(iterobj);

    return static_cast<PyObject*>(iterobj);
}


PyObject*
list_iternext(PyObject *self)
{
    ListIterObject *_self = static_cast<ListIterObject*>(self);

    if(_self->currentpos >= _self->list->getlength())
        return nullptr;

    PyObject *value = nullptr;
    int status = 0;

    std::tie(status, value) = _self->list->getter(_self->currentpos);
    if(status == ERROR){
        PyErr_SetNone(value);
        return nullptr;
    }

    Py_INCREF(value);
    _self->currentpos += 1;
    return value;
}


std::tuple<int, PyObject*>
ListObject::getter(Py_ssize_t index)
{
    if(index < 0 || index >= this->getlength())
        return std::make_tuple(ERROR, PyExc_IndexError);

    return std::make_tuple(SUCCESS, this->container[index]);
}


std::tuple<int, PyObject*>
ListObject::setter(Py_ssize_t index, PyObject *value)
{
    /* Check validity of index */
    if(index < 0 || index >= this->getlength())
        return std::make_tuple(ERROR, PyExc_IndexError);

    /* Change existing values stored at index */
    Py_CLEAR(this->container[index]);
    this->container[index] = value;
    return std::make_tuple(SUCCESS, nullptr);
}


std::tuple<int, PyObject*>
ListObject::deleter(Py_ssize_t index)
{
    /* Check validity of index */
    if(index < 0 || index >= this->getlength())
        return std::make_tuple(ERROR, PyExc_IndexError);

    /* Delete item */
    Py_CLEAR(this->container[index]);
    
    /* Shift list items after the deleted item */
    Py_ssize_t item_num = this->container.size();

    for(Py_ssize_t i = index + 1; i < item_num; ++i)
        this->container[i - 1] = this->container[i];

    this->container[item_num] = nullptr;
    
    /* Remove the last item */
    this->container.pop_back();

    /* Resize container if actual actual storage < 1/2 of capacity */
    --item_num;
    Py_ssize_t capacity = this->container.capacity(); 

    if(item_num < capacity / 2) 
        this->container.shrink_to_fit();

    return std::make_tuple(SUCCESS, nullptr);
}


static PyModuleDef list_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "list",
    .m_doc = "List object reimplemented in C++",
    .m_size = -1,
};

PyMODINIT_FUNC
PyInit_list(void)
{
    if(PyType_Ready(&ListType) < 0){
        PyErr_SetString(PyExc_RuntimeError, "Can not initialize ListType");
        return nullptr;
    }

    PyObject *module = PyModule_Create(&list_module);
    if(!module){
        PyErr_SetString(PyExc_RuntimeError, "Can not initialize list module");
        return nullptr;
    }

    int _check_list = PyModule_AddObject(module, "List", (PyObject*) &ListType);
    if(_check_list < 0){
        Py_DECREF(module);
        PyErr_SetString(PyExc_RuntimeError, "Can not add ListType to module");
        return nullptr;
    }

    return module;        
}
