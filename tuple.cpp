/*** A simplified version of C++ standard tuple ***/

#include <iostream>
#include <type_traits>

namespace
{
    using size_t = unsigned long;
};

//Define helper class to get element type from a variadic template
template<size_t idx, typename t, typename... Types>
    struct _Tuple_element
    {
        using type = typename _Tuple_element<idx - 1, Types...>::type;
    };

template<typename T, typename... Types>
    struct _Tuple_element<0ul, T, Types...>
    {
        using type = T;
    };

template<size_t idx, typename T, typename... Types>
    using _Tuple_element_t = typename _Tuple_element<idx, T, Types...>::type;

//Helper class to implement tuple
template<size_t idx, typename T>
    struct _Head_Base
    {
        explicit _Head_Base(T arg): value{arg} {};
        operator T() {return value;};
        T value;
    };

//_Tuple_impl is defined recursively
template<size_t idx, typename T, typename... Types>
    struct _Tuple_impl: public _Head_Base<idx, T>, public _Tuple_impl<idx + 1, Types...>
    {
        using _Base = _Head_Base<idx, T>;
        using _Inherited = _Tuple_impl<idx + 1, Types...>;
        explicit _Tuple_impl(T first_arg, Types... args): _Base{first_arg}, _Inherited{args...} {};

        template<size_t __id, typename U, typename... UTypes>
        friend std::ostream& operator<<(std::ostream& os, _Tuple_impl<__id, U, UTypes...> const& t);
    };

template<size_t idx, typename T>
    struct _Tuple_impl<idx, T>: public _Head_Base<idx, T>
    {
        using _Base = _Head_Base<idx, T>;
        _Tuple_impl(T arg): _Base{arg} {};
    };

//tuple class
template<typename... Types>
    class Tuple: public _Tuple_impl<0ul, Types...>
    {
    public:
        using _Impl = _Tuple_impl<0ul, Types...>;
        explicit Tuple(Types... args): _Impl{args...} {};

        friend std::ostream& operator<<(std::ostream& os, Tuple<Types...> const& t)
        {
            os << static_cast<_Impl>(t);
            return os;
        }
    };


//get tuple element by index
template<size_t idx, typename T, typename... Types>
    _Tuple_element_t<idx, T, Types...> get(Tuple<T, Types...> t)
    {
        static_assert(idx <= sizeof...(Types), "Index Overflow");
        using _RetType = _Tuple_element_t<idx, T, Types...>;
        using _Head = _Head_Base<idx, _RetType>;

        return static_cast<_Head>(t);
    }

//print all elements in a tuple
template<size_t idx, typename T, typename... Types>
    std::ostream& operator<<(std::ostream& os, _Tuple_impl<idx, T, Types...> const& t)
    {
        using _Base = _Head_Base<idx, T>;
        os << static_cast<_Base>(t) << '\n';

        if constexpr(sizeof...(Types) > 0){
            using _Inherited = _Tuple_impl<idx + 1, Types...>;
            os << static_cast<_Inherited>(t);
        }

        return os;
    }

//helper fucntion to form a tuple
template<typename... Types>
    Tuple<std::decay_t<Types>...> make_tuple(Types const&... args)
    {
        return Tuple{args...};
    }

int main()
{
    //a simple test case
    long x = 10l;
    double pi = 3.14159;
    char const* str = "Hello word";

    auto t = make_tuple(x, pi, str);
    std::cout << t;
}
