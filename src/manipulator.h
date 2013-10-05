//#pragma once
#ifndef MANIPULATOR_H_
#define MANIPULATOR_H_

#include <iostream>

namespace std {

    class width_prec
    {

    private:
        int _width;
        int _prec;

    public:

        width_prec (int width, int prec)
            : _width (width)
            , _prec (prec)
        {}

        template<class charT, class Traits>
        ::std::basic_ostream<charT, Traits>& operator() (::std::basic_ostream<charT, Traits> &os) const
        {
            os.setf (::std::ios_base::fixed, ::std::ios_base::floatfield);
            os.width (_width);
            os.precision (_prec);
            return os;
        }

    };

    template<class charT, class Traits>
    inline ::std::basic_ostream<charT, Traits>&
        operator<< (::std::basic_ostream<charT, Traits>& os, const width_prec &wp)
    {
        return wp (os);
    }


    // manip_infra is a small, intermediary class that serves as a utility
    // for custom manipulators with arguments.
    // Call its constructor with a function pointer and a value
    // from your main manipulator function.
    // The function pointer should be a helper function that does the actual work.
    // See examples below.
    template<class T, class C>
    class manip_infra
    {

    private:                      
        T _val;
        ::std::basic_ostream<C>& (*_fp_manip) (::std::basic_ostream<C>&, T);

    public:
        manip_infra (::std::basic_ostream<C>& (*fp_manip) (::std::basic_ostream<C>&, T), T val)
            : _fp_manip(fp_manip), _val(val)
        {}

        void operator() (::std::basic_ostream<C>& os) const
        {
            // Invoke the function pointer with the stream and value
            _fp_manip(os, _val);
        }  

    };

    template<class T, class C>
    inline ::std::basic_ostream<C>& operator<< (::std::basic_ostream<C>& os, const manip_infra<T, C>& manip)
    {
        manip (os);
        return os;
    }

    // Helper function that is ultimately called by the ManipInfra class
    inline ::std::ostream& set_width (::std::ostream& os, int n)
    {
        os.width (n);
        return (os);
    }

    // Manipulator function itself. This is what is used by client code
    inline manip_infra<int, char> set_width (int n)
    {
        return (manip_infra<int, char>(set_width, n));
    }

    // Another helper that takes a char argument
    inline ::std::ostream& set_fill (::std::ostream& os, char c)
    {
        os.fill (c);
        return (os);
    }

    inline manip_infra<char, char> set_fill(char c)
    {
        return (manip_infra<char, char>(set_fill, c));
    }


    //class eat
    //{
    //public:
    //    eat(char);
    //    ...
    //};

    //istream &operator>>(istream &, eat);


}

#endif