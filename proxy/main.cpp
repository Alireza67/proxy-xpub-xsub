#include <zmq.h>
#include <string>
#include <atomic>
#include <thread>
#include <iostream>
#include <mutex>
#include <vector>
#include <Windows.h>
#include <map>

using namespace std;

atomic<bool> kLiveFlag = true;
auto liveCouner = 0;
mutex lock_;

enum class COMMAND : uint16_t
{
	PAUSE = 1,
	RESUME = 2,
	TERMINATE = 3,
};

static map<COMMAND, string> MAP_ENUM_STR
{
	{COMMAND::PAUSE, "PAUSE"},
	{COMMAND::RESUME, "RESUME"},
	{COMMAND::TERMINATE, "TERMINATE"},
};

void Print(string msg)
{
	unique_lock<mutex>lk(lock_);
	cout << msg << endl;
}

using UniquePtrWithCustomDelete = unique_ptr<void, void(*)(void*)>;

void ProxySteerable(UniquePtrWithCustomDelete* context,
	vector<string> publishersAddresses, 
	string proxyPublisherAddress, 
	string captureAddress, 
	string controlAddress)
{
	auto xsub = zmq_socket(context->get(), ZMQ_XSUB);
	for (auto& item : publishersAddresses)
	{
		auto res = zmq_connect(xsub, item.c_str());
	}

	auto xpub = zmq_socket(context->get(), ZMQ_XPUB);
	auto res = zmq_bind(xpub, proxyPublisherAddress.c_str());

	auto capture = zmq_socket(context->get(), ZMQ_PUB);
	res = zmq_bind(capture, captureAddress.c_str());

	auto control = zmq_socket(context->get(), ZMQ_PULL);
	res = zmq_bind(control, controlAddress.c_str());
	auto timeout = 1000;
	res = zmq_setsockopt(control, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

	res = zmq_proxy_steerable(xsub, xpub, capture, control);
	cout << "CLOSE PROXY!" << endl;
	kLiveFlag.store(false);

	auto lingerTime = 0;
	res = zmq_setsockopt(xsub, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_setsockopt(xpub, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_setsockopt(capture, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_setsockopt(control, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(xsub);
	res = zmq_close(xpub);
	res = zmq_close(capture);
	res = zmq_close(control);
}

void Proxy(UniquePtrWithCustomDelete* context, vector<string> publisherAddresses, string proxyPublisherAddress, string captureAddress)
{
	auto capture = zmq_socket(context->get(), ZMQ_PUB);
	auto res = zmq_bind(capture, captureAddress.c_str());

	auto xpub = zmq_socket(context->get(), ZMQ_XPUB);
	res = zmq_bind(xpub, proxyPublisherAddress.c_str());

	auto xsub = zmq_socket(context->get(), ZMQ_XSUB);
	for (auto& item : publisherAddresses)
	{
		res = zmq_connect(xsub, item.c_str());
	}

	res = zmq_proxy(xsub, xpub, capture);
	cout << "CLOSE PROXY!" << endl;

	auto lingerTime = 0;
	res = zmq_setsockopt(xsub, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_setsockopt(xpub, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_setsockopt(capture, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(xsub);
	res = zmq_close(xpub);
	res = zmq_close(capture);
}

void Capture(UniquePtrWithCustomDelete* context, string captureAddress, vector<int> filters)
{
	auto receiver = zmq_socket(context->get(), ZMQ_SUB);
	auto res = zmq_connect(receiver, captureAddress.c_str());

	for (auto& item : filters)
	{
		res = zmq_setsockopt(receiver, ZMQ_SUBSCRIBE, &item, sizeof(item));
	}
	auto timeout = 1000;
	res = zmq_setsockopt(receiver, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

	int key;
	int message;

	while (kLiveFlag.load())
	{
		res = zmq_recv(receiver, &key, sizeof(key), 0);
		if (res < 0)
		{
			continue;
		}

		res = zmq_recv(receiver, &message, sizeof(message), 0);
		auto msg = "Capture receive: key: " + to_string(key) + " value: " + to_string(message) + "\n";
		OutputDebugStringA(msg.c_str());
	}
	auto lingerTime = 0;
	res = zmq_setsockopt(receiver, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(receiver);
}

void Control(UniquePtrWithCustomDelete* context, string controlAddress)
{
	auto sender = zmq_socket(context->get(), ZMQ_PUSH);
	auto res = zmq_connect(sender, controlAddress.c_str());

	uint16_t temp;

	while (kLiveFlag.load())
	{
		cout << "Enter Command: 1) PAUSE  2) RESUME  3) TERMINATE\n";
		cin >> temp;
		auto command = MAP_ENUM_STR.at(static_cast<COMMAND>(temp));
		res = zmq_send(sender, command.data(), command.size(), 0);
		if (static_cast<COMMAND>(temp) == COMMAND::TERMINATE)
		{
			break;
		}
	}

	auto lingerTime = 0;
	res = zmq_setsockopt(sender, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(sender);
}

void Publisher(UniquePtrWithCustomDelete* context, string name, string address, int filter, int message)
{
	auto socketSender = zmq_socket(context->get(), ZMQ_PUB);
	auto res = zmq_bind(socketSender, address.c_str());

	while (kLiveFlag.load())
	{
		res = zmq_send(socketSender, &filter, sizeof(filter), ZMQ_SNDMORE);
		res = zmq_send(socketSender, &message, sizeof(message), 0);
		this_thread::sleep_for(1s);
		message++;
	}

	auto lingerTime = 0;
	res = zmq_setsockopt(socketSender, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(socketSender);
}

void Subscriber(UniquePtrWithCustomDelete* context, string name, string ProxyAddress, int filter)
{
	auto socketReceiver = zmq_socket(context->get(), ZMQ_SUB);
	auto res = zmq_connect(socketReceiver, ProxyAddress.c_str());
	res = zmq_setsockopt(socketReceiver, ZMQ_SUBSCRIBE, &filter, sizeof(filter));
	auto timeout = 1000;
	res = zmq_setsockopt(socketReceiver, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

	int key;
	int buffer;

	while (kLiveFlag.load())
	{
		res = zmq_recv(socketReceiver, &key, sizeof(key), 0);
		if (res < 0)
			continue;
		res = zmq_recv(socketReceiver, &buffer, sizeof(buffer), 0);

		auto msg = "Subscriber (" + name + ") receive: key: " + to_string(key) + " value: " + to_string(buffer) + "\n";
		OutputDebugStringA(msg.c_str());
	}

	auto lingerTime = 0;
	res = zmq_setsockopt(socketReceiver, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(socketReceiver);
}

void ResolveSlowJoinerSyndrome()
{
	this_thread::sleep_for(1s);
}

int main()
{

	unique_ptr<void, void(*)(void*)> context(zmq_ctx_new(), [](void* ctx) 
		{
			auto res = zmq_ctx_shutdown(ctx);
			res = zmq_ctx_destroy(ctx);
		});


	auto publisherPort = 9000;
	auto publisherAddress = "inproc://job_1";

	auto publisherPort2 = 9001;
	auto publisherAddress2 = "inproc://job_2";

	auto publishersAddresses = vector<string>{ publisherAddress, publisherAddress2 };

	auto proxyPublisherPort = 10000;
	auto proxyPublisherAddress = "tcp://127.0.0.1:"s + to_string(proxyPublisherPort);

	auto captureAddress = "inproc://capture"s;
	auto controlAddress = "inproc://control"s;

	auto filter1 = 66;
	auto filter2 = 77;
	auto filters = vector<int>{ filter1, filter2 };
	auto proxy = thread(ProxySteerable, &context, publishersAddresses, proxyPublisherAddress, captureAddress, controlAddress);
	ResolveSlowJoinerSyndrome();
	auto pub2 = thread(Publisher, &context, "pub2"s, publisherAddress2, filter2, 0);
	auto pub1 = thread(Publisher, &context, "pub1"s, publisherAddress, filter1, 100);
	auto sub1 = thread(Subscriber, &context, "sub1"s, proxyPublisherAddress, 66);
	auto sub2 = thread(Subscriber, &context, "sub2"s, proxyPublisherAddress, 77);
	auto capture = thread(Capture, &context, captureAddress, filters);
	auto control = thread(Control, &context, controlAddress);

	while (kLiveFlag.load())
	{
		this_thread::sleep_for(1s);
	}

	proxy.join();
	pub1.join();
	pub2.join();
	sub1.join();
	sub2.join();
	capture.join();
	control.join();
}
