#include <signal.h>
#include "signal.hh"



static volatile sig_atomic_t e_sigint_flag = 0;



namespace gpsxre
{

Signal &Signal::GetInstance()
{
	static Signal instance;

	return instance;
}


void Signal::Engage()
{
	e_sigint_flag = 1;
}


void Signal::Disengage()
{
	e_sigint_flag = 0;
}


bool Signal::IsEngaged()
{
	return e_sigint_flag;
}


Signal::Signal()
{
	signal(SIGINT, Handler);
}


void Signal::Handler(int sig)
{
	if(e_sigint_flag)
		e_sigint_flag = 0;
	else
	{
		signal(sig, SIG_DFL);
		raise(sig);
	}
}

}
