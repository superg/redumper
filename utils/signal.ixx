module;
#include <signal.h>

export module utils.signal;



static volatile sig_atomic_t g_sigint_flag = 0;



namespace gpsxre
{

export class Signal
{
public:
	static Signal &get()
	{
		static Signal instance;

		return instance;
	}


	void engage()
	{
		g_sigint_flag = 2;
	}


	void disengage()
	{
		g_sigint_flag = 0;
	}


	bool interrupt()
	{
		return g_sigint_flag == 1;
	}


	Signal(Signal const &) = delete;
	void operator=(Signal const &) = delete;

private:
	Signal()
	{
		signal(SIGINT, handler);
	}


	static void handler(int sig)
	{
		if(!g_sigint_flag)
		{
			signal(sig, SIG_DFL);
			raise(sig);
		}
		else if(g_sigint_flag == 2)
			g_sigint_flag = 1;
	}
};

}
