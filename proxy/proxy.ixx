module;
#include <zmq.h>

export module proxy;

import <map>;
import <memory>;
import <string>;
import <vector>;
import <thread>;
import <iostream>;
import <stdexcept>;

using namespace std::literals;
export using UniquePtrWithCustomDelete = std::unique_ptr<void, void(*)(void*)>;


export enum class COMMAND : uint16_t
{
	PAUSE = 1,
	RESUME = 2,
	TERMINATE = 3,
};

export std::map<int, COMMAND> MAP_INT_ENUM
{
	{1, COMMAND::PAUSE},
	{2, COMMAND::RESUME},
	{3, COMMAND::TERMINATE}
};

static std::map<COMMAND, std::string> MAP_ENUM_STR
{
	{COMMAND::PAUSE, "PAUSE"},
	{COMMAND::RESUME, "RESUME"},
	{COMMAND::TERMINATE, "TERMINATE"},
};

export class ProxySteerable
{
public:
	explicit ProxySteerable(
		UniquePtrWithCustomDelete& context,
		std::vector<std::string> publishersAddresses,
		std::string proxyPublisherAddress,
		std::string captureAddress,
		std::string controlAddress);

	virtual ~ProxySteerable();
	
	bool StartProxy();
	bool ControlProxy(COMMAND command);

	ProxySteerable(const ProxySteerable& rhs) = delete;
	ProxySteerable(ProxySteerable&& rhs) = delete;
	ProxySteerable& operator=(const ProxySteerable& rhs) = delete;
	ProxySteerable& operator=(ProxySteerable&& rhs) = delete;

private:
	void* xsub_{};
	void* xpub_{};
	void* capture_{};
	void* control_{};
	void* commander_{};
	std::thread mainThread_{};
	std::string controlAddress_{};
	UniquePtrWithCustomDelete& context_;

	void Run();
	void CloseSockets();
	void InitializeCaptureSocket(std::string& captureAddress);
	void InitializeContorlSocket(std::string& controlAddress);
	void InitializeCommanderSocket(std::string& controlAddress);
	void InitializeXpubSocket(std::string& proxyPublisherAddress);
	void InitializeXsubSocket(std::vector<std::string>& publishersAddresses);
};

ProxySteerable::ProxySteerable(
	UniquePtrWithCustomDelete& context, 
	std::vector<std::string> publishersAddresses, 
	std::string proxyPublisherAddress, 
	std::string captureAddress, 
	std::string controlAddress)
	: controlAddress_(controlAddress), context_(context)
{
	InitializeXsubSocket(publishersAddresses);
	InitializeXpubSocket(proxyPublisherAddress);
	InitializeCaptureSocket(captureAddress);
	InitializeContorlSocket(controlAddress);
	InitializeCommanderSocket(controlAddress);
}

void ProxySteerable::InitializeCommanderSocket(std::string& controlAddress)
{
	commander_ = zmq_socket(context_.get(), ZMQ_PUSH);
	if (!commander_)
	{
		throw std::runtime_error("Proxy couldn't create commander socket!"s);
	}

	auto res = zmq_connect(commander_, controlAddress.c_str());
	if (res)
	{
		throw std::runtime_error("Commander socket couldn't connect!"s);
	}
}

ProxySteerable::~ProxySteerable()
{
	if (mainThread_.joinable())
	{
		if (ControlProxy(COMMAND::TERMINATE))
		{
			mainThread_.join();
			CloseSockets();
		}
		else
		{
			std::cerr << "Couldn't stop proxy thread!\n";
		}
	}
}

void ProxySteerable::CloseSockets()
{
	auto lingerTime = 0;
	zmq_setsockopt(xsub_, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	zmq_setsockopt(xpub_, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	zmq_setsockopt(capture_, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	zmq_setsockopt(control_, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	zmq_setsockopt(commander_, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	zmq_close(xsub_);
	zmq_close(xpub_);
	zmq_close(capture_);
	zmq_close(control_);
	zmq_close(commander_);
}

void ProxySteerable::Run()
{
	auto res = zmq_proxy_steerable(xsub_, xpub_, capture_, control_);
	std::cout << "CLOSE PROXY!" << '\n';
}

bool ProxySteerable::ControlProxy(COMMAND command)
{
	auto commandStr = MAP_ENUM_STR.at(command);
	auto res = zmq_send(commander_, commandStr.data(), commandStr.size(), 0);
	if (res)
	{
		return true;
	}
	return false;
}

bool ProxySteerable::StartProxy()
{
	if (!mainThread_.joinable())
	{
		mainThread_ = std::thread(&ProxySteerable::Run, this);
		return true;
	}
	return false;
}

void ProxySteerable::InitializeContorlSocket(std::string& controlAddress)
{
	control_ = zmq_socket(context_.get(), ZMQ_PULL);
	if (!control_)
	{
		throw std::runtime_error("Proxy couldn't create control socket!"s);
	}

	auto res = zmq_bind(control_, controlAddress.c_str());
	if (res)
	{
		throw std::runtime_error("Proxy couldn't establish on control port!"s);
	}

	auto timeout = 1000;
	zmq_setsockopt(control_, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
}

void ProxySteerable::InitializeCaptureSocket(std::string& captureAddress)
{
	capture_ = zmq_socket(context_.get(), ZMQ_PUB);
	if (!capture_)
	{
		throw std::runtime_error("Proxy couldn't create capture socket!"s);
	}

	auto res = zmq_bind(capture_, captureAddress.c_str());
	if (res)
	{
		throw std::runtime_error("Proxy couldn't establish on capture port!"s);
	}
}

void ProxySteerable::InitializeXpubSocket(std::string& proxyPublisherAddress)
{
	xpub_ = zmq_socket(context_.get(), ZMQ_XPUB);
	if (!xpub_)
	{
		throw std::runtime_error("Proxy couldn't create xpub socket!"s);
	}

	auto res = zmq_bind(xpub_, proxyPublisherAddress.c_str());
	if (res)
	{
		throw std::runtime_error("Proxy couldn't establish on publisher port!"s);
	}
}

void ProxySteerable::InitializeXsubSocket(std::vector<std::string>& publishersAddresses)
{
	xsub_ = zmq_socket(context_.get(), ZMQ_XSUB);
	if (!xsub_)
	{
		throw std::runtime_error("Proxy couldn't create xsub socket!"s);
	}

	for (auto& item : publishersAddresses)
	{
		auto res = zmq_connect(xsub_, item.c_str());
		if (res)
		{
			throw std::runtime_error("Proxy couldn't connect to pulishers: "s + item);
		}
	}
}
