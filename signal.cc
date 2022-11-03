#include <signal.h>
#include "signal.hh"



static volatile sig_atomic_t g_sigint_flag = 0;



namespace gpsxre
{

Signal &Signal::GetInstance()
{
	static Signal instance;

	return instance;
}


void Signal::Engage()
{
	g_sigint_flag = 2;
}


void Signal::Disengage()
{
	g_sigint_flag = 0;
}


bool Signal::Interrupt()
{
	return g_sigint_flag == 1;
}


Signal::Signal()
{
	signal(SIGINT, Handler);
}


void Signal::Handler(int sig)
{
	if(!g_sigint_flag)
	{
		signal(sig, SIG_DFL);
		raise(sig);
	}
	else if(g_sigint_flag == 2)
		g_sigint_flag = 1;
}

}
