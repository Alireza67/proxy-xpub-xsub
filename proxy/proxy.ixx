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
import <stop_token>;

using namespace std::literals;
using UniquePtrWithCustomDelete = std::unique_ptr<void, void(*)(void*)>;


export enum class COMMAND : uint16_t
{
	PAUSE = 1,
	RESUME = 2,
	TERMINATE = 3,
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
	std::thread mainThread_{};
	std::string controlAddress_{};
	UniquePtrWithCustomDelete& context_;

	void Run(std::stop_token stoken);
	void InitializeCaptureSocket(std::string& captureAddress);
	void InitializeContorlSocket(std::string& controlAddress);
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
}

ProxySteerable::~ProxySteerable()
{
	auto sender = zmq_socket(context_.get(), ZMQ_PUSH);
	auto res = zmq_connect(sender, controlAddress_.c_str());

	auto command = MAP_ENUM_STR.at(static_cast<COMMAND>(temp));
	res = zmq_send(sender, command.data(), command.size(), 0);
}

bool ProxySteerable::ControlProxy(COMMAND command)
{
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

void ProxySteerable::Run(std::stop_token stoken)
{
	auto res = zmq_proxy_steerable(xsub_, xpub_, capture_, control_);
	std::cout << "CLOSE PROXY!" << '\n';
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
