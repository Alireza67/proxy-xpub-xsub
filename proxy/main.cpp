#include <zmq.h>
#include <string>
#include <atomic>
#include <thread>
#include <iostream>
#include <mutex>
#include <vector>
#include <Windows.h>
#include <map>
#include <charconv>
#include <list>
#include <stop_token>

import proxy;

using namespace std;


atomic<bool> liveFlag = true;
auto liveCouner = 0;
mutex lock_;


void Print(string msg)
{
	unique_lock<mutex>lk(lock_);
	cout << msg << endl;
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

void Capture(std::stop_token stoken, UniquePtrWithCustomDelete* context, string captureAddress, vector<int> filters)
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

	while (!stoken.stop_requested())
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

void Control(std::stop_token stoken, std::unique_ptr<ProxySteerable>* proxy)
{
	while (!stoken.stop_requested())
	{
		std::string input;
		cout << "Enter Command: 1) PAUSE  2) RESUME  3) TERMINATE\n";
		cin >> input;
		
		int value;
		auto [ptr, error] {std::from_chars(input.data(), input.data() + input.size(), value)};
		if (error == std::errc{})
		{
			if(MAP_INT_ENUM.count(value))
			{
				if (!(*proxy)->ControlProxy(MAP_INT_ENUM[value]))
				{
					std::cerr << "Command Failed!\n";
				}

				if (MAP_INT_ENUM[value] == COMMAND::TERMINATE)
				{
					liveFlag = false;
				}
				continue;
			}
		}
		std::cerr << "Input command isn't valid!\n";
	}
}

void Publisher(std::stop_token stoken, UniquePtrWithCustomDelete* context, string name, string address, int filter, int message)
{
	auto socketSender = zmq_socket(context->get(), ZMQ_PUB);
	auto res = zmq_bind(socketSender, address.c_str());

	while (!stoken.stop_requested())
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

void Subscriber(std::stop_token stoken, UniquePtrWithCustomDelete* context, string name, string ProxyAddress, int filter)
{
	auto socketReceiver = zmq_socket(context->get(), ZMQ_SUB);
	auto res = zmq_connect(socketReceiver, ProxyAddress.c_str());
	res = zmq_setsockopt(socketReceiver, ZMQ_SUBSCRIBE, &filter, sizeof(filter));
	auto timeout = 1000;
	res = zmq_setsockopt(socketReceiver, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

	int key;
	int buffer;

	while (!stoken.stop_requested())
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
	//============== Context ===============//
	UniquePtrWithCustomDelete context(zmq_ctx_new(), [](void* ctx)
		{
			auto res = zmq_ctx_shutdown(ctx);
			res = zmq_ctx_destroy(ctx);
		});

	//============== Proxy ===============//
	auto publishersAddresses = vector<string>{ "inproc://job_1", "inproc://job_2" };
	auto proxyPublisherAddress = "tcp://127.0.0.1:10000"s;
	auto captureAddress = "inproc://capture"s;
	auto controlAddress = "inproc://control"s;

	auto proxy = std::make_unique<ProxySteerable>(
		context, 
		publishersAddresses, 
		proxyPublisherAddress, 
		captureAddress, 
		controlAddress);

	proxy->StartProxy();

	ResolveSlowJoinerSyndrome();

	//============== Publishers ===============//
	auto filters = std::vector<int>{ 66, 77 };
	auto initialMessage = std::vector<int>{ 0, 100 };
	auto publishersNames = vector<string>{ "pub1", "pub2" };
	std::list<std::jthread> publishers;

	auto counter{ 0 };
	for (auto& itemName : publishersNames)
	{
		publishers.emplace_back(Publisher, 
			&context, itemName, publishersAddresses[counter], filters[counter], initialMessage[counter]);
	}

	//============== Subscriberes ===============//
	auto subscribersNames = vector<string>{ "sub1", "sub2" };
	std::list<std::jthread> subscribers;

	counter = 0;
	for (auto& itemName : subscribersNames)
	{
		subscribers.emplace_back(Subscriber,
			&context, itemName, proxyPublisherAddress, filters[counter]);
	}

	//============== Capture ===============//
	auto capture = std::jthread(Capture, &context, captureAddress, filters);

	//============== Control ===============//
	auto control = std::jthread(Control, &proxy);

	while (liveFlag.load())
	{
		this_thread::sleep_for(1s);
	}

	//============== Clean-up ===============//
	for (auto& item : publishers)
	{
		item.request_stop();
		item.join();
	}

	for (auto& item : subscribers)
	{
		item.request_stop();
		item.join();
	}

	capture.request_stop();
	capture.join();

	control.request_stop();
	control.join();
}
