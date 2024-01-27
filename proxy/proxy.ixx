module;
#include <zmq.h>

export module proxy;

import <memory>;
import <string>;
import <vector>;
import <stdexcept>;

using namespace std::literals;
using UniquePtrWithCustomDelete = std::unique_ptr<void, void(*)(void*)>;

export class ProxySteerable
{
public:
	explicit ProxySteerable(
		UniquePtrWithCustomDelete& context,
		std::vector<std::string> publishersAddresses,
		std::string proxyPublisherAddress,
		std::string captureAddress,
		std::string controlAddress);

	virtual ~ProxySteerable() = default;

	void InitializeCaptureSocket(std::string& captureAddress);
	void InitializeContorlSocket(std::string& controlAddress);
	void InitializeXpubSocket(std::string& proxyPublisherAddress);
	void InitializeXsubSocket(std::vector<std::string>& publishersAddresses);

	ProxySteerable(const ProxySteerable& rhs) = delete;
	ProxySteerable(ProxySteerable&& rhs) = delete;
	ProxySteerable& operator=(const ProxySteerable& rhs) = delete;
	ProxySteerable& operator=(ProxySteerable&& rhs) = delete;

private:
	void* xsub_{};
	void* xpub_{};
	void* capture_{};
	void* control_{};

	UniquePtrWithCustomDelete& context_;
};

ProxySteerable::ProxySteerable(
	UniquePtrWithCustomDelete& context, 
	std::vector<std::string> publishersAddresses, 
	std::string proxyPublisherAddress, 
	std::string captureAddress, 
	std::string controlAddress)
	: context_(context)
{
	InitializeXsubSocket(publishersAddresses);
	InitializeXpubSocket(proxyPublisherAddress);
	InitializeCaptureSocket(captureAddress);
	InitializeContorlSocket(controlAddress);
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
