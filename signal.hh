#pragma once



namespace gpsxre
{

class Signal
{
public:
	static Signal &GetInstance();

	void Engage();
	void Disengage();
	bool IsEngaged();

	Signal(Signal const &) = delete;
	void operator=(Signal const &) = delete;

private:
	Signal();

	static void Handler(int sig);
};

}
