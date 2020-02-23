/* Reimplement Python's dictionary
 * using hash table in C++
 * Only accept string as key
 * Deletion of key is not supported
 */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <cassert>
#include <vector>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <exception>

static constexpr Py_ssize_t DICT_MIN_SIZE = 8;
static constexpr double LOAD_FACTOR = 2.0 / 3.0;

struct KeyError: public std::exception
{
    virtual const char *what() const noexcept
    {
        return "Key not found error";
    }
};

struct DictEntry 
{
    DictEntry() = default;
    DictEntry& operator=(DictEntry&&) = default;
    ~DictEntry() = default;
    bool empty() {return key.empty();};

    std::string key;
    Py_ssize_t index; /*address to value*/
};

struct DictIterObject;

class DictObject: public PyObject
{
public:
    DictObject(void);
    DictObject(DictObject const&) = delete;
    DictObject operator=(DictObject const&) = delete;
    DictObject(DictObject&&) = delete;
    DictObject operator=(DictObject&&) = delete;
    ~DictObject();

    friend PyObject* dict_iternext(PyObject*);

    Py_ssize_t size() const {return values.size();};
    PyObject *at(size_t index) const {return values.at(index);};
    std::tuple<int, Py_ssize_t, Py_ssize_t> get_item(std::string const&) const;
    void set_item(std::string&& key, PyObject *value);
    
private: /* data members */
    Py_ssize_t hashsize;
    std::unique_ptr<DictEntry[]> hashtable;
    std::vector<PyObject*> values;

private: /* helper methods */
    Py_hash_t probe(Py_hash_t hashpos) const;
    Py_hash_t hash(std::string key) const ;
    void resize();
    bool is_power_2(Py_ssize_t x) const;
    bool is_full_load() const;
};

static PyObject *dict_new(PyTypeObject*, PyObject*, PyObject*);
static int dict_init(PyObject*, PyObject*, PyObject*);
static void dict_dealloc(PyObject *self);

/* mapping protocol */
static Py_ssize_t dict_getlen(PyObject* self);
static PyObject *dict_subscript(PyObject *self, PyObject *key);
static int dict_ass_subscript(PyObject *self, PyObject *key, PyObject *value);
static PyMappingMethods dict_mapping = {
    .mp_length = dict_getlen,
    .mp_subscript = dict_subscript,
    .mp_ass_subscript = dict_ass_subscript,
};

/* iterator protocol */
PyObject *dict_iter(PyObject *self);
PyObject *dict_iternext(PyObject *self);


struct DictIterObject : public PyObject
{
    DictIterObject(DictObject *_dictobj);
    ~DictIterObject() 
    {
        assert(dictobj == NULL && "dictobj ptr is not NULL");
    };

    DictObject *dictobj;
    Py_ssize_t iterpos;
};

static PyObject *dictiter_new(PyTypeObject*, PyObject*, PyObject*);
static void dictiter_dealloc(PyObject *self);

static PyTypeObject DictIterType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "DictIter",
    .tp_doc = "Support iteration of DictType",
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_basicsize = sizeof(DictIterObject),
    .tp_itemsize = 0,
    .tp_new = dictiter_new,
    .tp_dealloc = dictiter_dealloc,
    .tp_iter = dict_iter,
    .tp_iternext = dict_iternext,
};


DictIterObject::DictIterObject(DictObject *_dictobj)
        : PyObject{0, &DictIterType}, 
          dictobj{_dictobj}, iterpos{0l}
{
    Py_INCREF(this->dictobj);
}


static PyObject *dictiter_new(PyTypeObject*, PyObject *args, PyObject*)
{
    DictObject *dictobj = NULL;
    if(!PyArg_ParseTuple(args, "O:set_dict", &dictobj))
        return NULL;

    DictIterObject *self = new DictIterObject{dictobj};
    return static_cast<PyObject*>(self);
}

static void dictiter_dealloc(PyObject *self)
{
    DictIterObject *_self = static_cast<DictIterObject*>(self);
    Py_CLEAR(_self->dictobj);
    delete _self;
}


/* Definition of DictType */
static PyTypeObject DictType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "Dict",
    .tp_doc = "Cutom dictionary\n" 
              "Only string can be used as key.\n"
              "Delete item is not supported.",

    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_basicsize = sizeof(DictObject),
    .tp_itemsize = 0,
    .tp_new = dict_new,
    .tp_init = dict_init,
    .tp_dealloc = dict_dealloc,
    .tp_as_mapping = &dict_mapping,
    .tp_iter = &dict_iter,
};

DictObject::DictObject(): PyObject{0, &DictType}, 
                          hashsize{DICT_MIN_SIZE},
                          hashtable{new DictEntry[hashsize]}, 
                          values{}
{/* Empty body */}


DictObject::~DictObject()
{
    for(std::size_t i = 0; i < this->values.size(); ++i)
        Py_CLEAR(values[i]);
}


Py_hash_t
DictObject::probe(Py_hash_t hashpos) const
{
    Py_hash_t hashsize = this->hashsize;
    assert(is_power_2(hashsize) && "size of hash table is not power of 2");

    Py_hash_t new_hashpos = (5 * hashpos + 1) & (hashsize - 1);
    return new_hashpos;
}
    

Py_hash_t 
DictObject::hash(std::string key) const
{
    Py_hash_t result = 0;
    for(std::size_t i = 0; i < key.size(); ++i){
        Py_hash_t c = key[i];
        result = c + (result << 6) + (result << 16) - result;
    }

    return result;
}


bool
DictObject::is_power_2(Py_ssize_t x) const
{
    return (x & (x - 1)) == 0;
}


bool
DictObject::is_full_load() const
{
    size_t max_load = (size_t) (hashsize * LOAD_FACTOR);
    return this->values.size() >= max_load;
}


enum hash_slot_status
{
    EMPTY,
    OCCUPIED
};

std::tuple<int, Py_ssize_t, Py_ssize_t> 
DictObject::get_item(std::string const& key) const
{
    /* return status, index, hashpos
     * status is either EMPTY or OCCUPIED
     * index is location of PyObject* in values vector (-1 on empty)
     * hashpos is location of the result in hashtable
     */
    Py_ssize_t hashvalue = hash(key);
    Py_ssize_t hashmask = hashsize - 1;
    Py_ssize_t hashpos = hashvalue & hashmask;

    while(!hashtable[hashpos].empty()){
        if(hashtable[hashpos].key == key){
            Py_ssize_t index = hashtable[hashpos].index;
            return std::make_tuple(OCCUPIED, index, hashpos);
        }

        hashpos = probe(hashpos);
    }

    return std::make_tuple(EMPTY, -1, hashpos);
}


void
DictObject::resize()
{
    /* resize (double) hashtable when fully loaded
     * return void and set data member hashtable, hashsize when success
     * throw bad_alloc on failure
     * if error occur, hashtable and hashsize stay intact
     */ 

    // allocate memory for new hashtable
    Py_ssize_t oldsize = this->hashsize;
    Py_ssize_t newsize = 2 * oldsize;

    //May throw bad_alloc
    std::unique_ptr<DictEntry[]> tmp {new DictEntry[newsize]};

    //safe to swap tmp and hashtable
    this->hashsize = newsize;
    std::swap(tmp, this->hashtable);

    for(size_t idx = 0; idx < oldsize; ++idx){
        DictEntry& entry = tmp[idx];
        if(entry.empty())
            continue;

        int status = 0;
        Py_ssize_t hashpos = 0;
        std::tie(status, std::ignore, hashpos) = get_item(entry.key);
        assert(status == EMPTY && "logic error");
        
        this->hashtable[hashpos] = std::move(entry);
    }
}


void
DictObject::set_item(std::string&& key, PyObject *value)
{
    /* set key, value pair to hashtable
     * resize if perform if needed
     * return void on success
     * throw bad_alloc if no memory is available when resize
     */
    if(this->is_full_load())
        this->resize();

    int status = 0;
    Py_ssize_t index = 0;
    Py_ssize_t hashpos = 0;

    std::tie(status, index, hashpos) = get_item(key);
    DictEntry& entry = hashtable[hashpos];
    Py_INCREF(value);

    if(status == EMPTY){
        values.push_back(value);
        entry.index = values.size() - 1;
        entry.key = std::move(key);    
    } else { // status == OCCUPIED
        PyObject *old_value = values[index];        
        values[index] = value;
        Py_DECREF(old_value);
    }
}


/* Define method for DictObject */
static PyObject*
dict_new(PyTypeObject*, PyObject*, PyObject*)
{
    DictObject *self = new DictObject{};
    Py_INCREF(self);
    return static_cast<PyObject*> (self);
}


static int 
dict_init(PyObject*, PyObject*, PyObject*)
{
    return 0;
};


static void
dict_dealloc(PyObject *self)
{
    delete static_cast<DictObject*>(self);/*dtor of DictObject do the work*/
}


static Py_ssize_t 
dict_getlen(PyObject* self)
{
    DictObject *_self = static_cast<DictObject*>(self);
    return _self->size();
}


static PyObject*
dict_subscript(PyObject *self, PyObject *key)
{
    DictObject const *_self = static_cast<DictObject*>(self);
    if(!PyUnicode_Check(key)){
        PyErr_SetString(PyExc_TypeError, "Argument must be of type string");
        return NULL;
    }
     
    std::string _key {PyUnicode_AsUTF8(key)};
    int status = 0;
    Py_ssize_t index = 0;

    std::tie(status, index, std::ignore) = _self->get_item(_key);
    if(status == EMPTY){
        PyErr_Format(PyExc_KeyError, "%s not found", _key.c_str());
        return NULL;
    }

    /* status == OCCUPIED */
    PyObject *result = _self->at(index);
    Py_INCREF(result);
    return result;
}


static int
dict_ass_subscript(PyObject *self, PyObject *key, PyObject *value)
{
    //delitem is not supported
    if(!value){
        PyErr_SetString(PyExc_NotImplementedError, "DictObject is append only");
        return -1;
    }
    
    DictObject *_self = static_cast<DictObject*>(self);

    if(!PyUnicode_Check(key)){
        PyErr_SetString(PyExc_KeyError, "Key must be of type string");
        return -1;
    }

    try
    {
        _self->set_item(std::string {PyUnicode_AsUTF8(key)}, value);
    }
    catch (std::bad_alloc)
    {
        PyErr_NoMemory();
        return -1;
    }
        
    return 0;
}


PyObject*
dict_iter(PyObject *self)
{
    if(self->ob_type == &DictIterType)
        return self;

    DictObject *_self = static_cast<DictObject*>(self);
    DictIterObject *iterobj = new DictIterObject{_self};
    Py_INCREF(iterobj); 
    return static_cast<PyObject*>(iterobj);
}


PyObject*
dict_iternext(PyObject *self)
{
    DictIterObject *_self = static_cast<DictIterObject*>(self);
    DictObject *dictobj = _self->dictobj;

    while(_self->iterpos < dictobj->hashsize 
            && dictobj->hashtable[_self->iterpos].empty())
    {
        _self->iterpos += 1; 
    }
    
    if(_self->iterpos >= dictobj->hashsize)
        return NULL;

    DictEntry& entry = dictobj->hashtable[_self->iterpos];
    const char *key = entry.key.c_str();
    PyObject *value = dictobj->values[entry.index];

    PyObject *result = Py_BuildValue("(sO)", key, value);
    _self->iterpos += 1; 

    return result;
};


/* Initialize new module */
static PyModuleDef dict_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "dict",
    .m_doc = "dictionary reimplemented in C++",
    .m_size = -1,
};


PyMODINIT_FUNC
PyInit_dict(void)
{
    /* Initialize DictType */
    if(PyType_Ready(&DictType) < 0){
        PyErr_SetString(PyExc_RuntimeError, 
                        "Can not initialize DictType");
        return NULL;
    }

    if(PyType_Ready(&DictIterType) < 0){
        PyErr_SetString(PyExc_RuntimeError, 
                        "Can not initialize DictIterType");
        return NULL;
    }
    
    /* Initialize module */
    PyObject *module = PyModule_Create(&dict_module);
    if(!module){
        PyErr_SetString(PyExc_RuntimeError,
                        "Can not initialize dict module");
        return NULL;
    }

    /* Add DictType to module */
    int _check_dict = PyModule_AddObject(module, "Dict", 
                                         (PyObject*) &DictType);

    int _check_dictiter = PyModule_AddObject(module, "DictIter", 
                                            (PyObject*) &DictIterType);

    if(_check_dict < 0 || _check_dictiter < 0)
        goto AddObjectFail; 

    return module;

AddObjectFail:
    Py_DECREF(&module);
    PyErr_SetString(PyExc_RuntimeError,
                    "Can not add custom type to dict module");
    return NULL;
}
