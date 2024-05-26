module;
#include <signal.h>
#include "throw_line.hh"

export module utils.signal;



namespace gpsxre
{

template<int S>
class Signal
{
public:
    Signal()
    {
        setHandler();
        engage();
    }


    ~Signal()
    {
        disengage();
        resetHandler();
    }


    void engage()
    {
        _flag = 2;
    }


    void disengage()
    {
        _flag = 0;
    }


    bool interrupt()
    {
        return _flag == 1;
    }


    static void raiseDefault()
    {
        resetHandler();
        ::raise(S);
        setHandler();
    }


    Signal(Signal const &) = delete;
    void operator=(Signal const &) = delete;

private:
    static volatile sig_atomic_t _flag;

    static void setHandler()
    {
        auto old_handler = signal(S, handler);
        if(old_handler != SIG_DFL)
        {
            signal(S, old_handler);
            throw_line("signal handler already set (signal: {})", S);
        }
    }


    static void resetHandler()
    {
        signal(S, SIG_DFL);
    }


    static void handler(int)
    {
        if(!_flag)
            raiseDefault();
        else if(_flag == 2)
            _flag = 1;
    }
};

template<int S>
volatile sig_atomic_t Signal<S>::_flag;

export using SignalINT = Signal<SIGINT>;

}
