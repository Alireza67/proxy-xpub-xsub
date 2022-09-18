#include <zmq.h>
#include <string>
#include <atomic>
#include <thread>
#include <iostream>
#include <mutex>
#include <vector>

using namespace std;

atomic<bool> kLiveFlag = true;
mutex lock_;

void Print(string msg)
{
	unique_lock<mutex>lk(lock_);
	cout << msg << endl;
}

void Proxy(vector<string> publisherAddresses, string proxyPublisherAddress)
{
	auto ctx = zmq_ctx_new();

	auto xpub = zmq_socket(ctx, ZMQ_XPUB);
	auto res = zmq_bind(xpub, proxyPublisherAddress.c_str());

	auto xsub = zmq_socket(ctx, ZMQ_XSUB);
	for (auto& item : publisherAddresses)
	{
		res = zmq_connect(xsub, item.c_str());
	}	

	res = zmq_proxy(xsub, xpub, NULL);

	while (kLiveFlag)
	{
		this_thread::sleep_for(1s);
	}

	auto lingerTime = 0;
	res = zmq_setsockopt(xsub, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_setsockopt(xpub, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(xsub);
	res = zmq_close(xpub);
	res = zmq_ctx_destroy(ctx);
}

void Publisher(string name, string address, int message)
{
	auto ctx = zmq_ctx_new();
	auto socketSender = zmq_socket(ctx, ZMQ_PUB);
	auto res = zmq_bind(socketSender, address.c_str());

	while (kLiveFlag.load())
	{
		res = zmq_send(socketSender, &message, sizeof(message), 0);
		if (res)
		{
			//auto msg = "Publisher (" + name + ") send: " + to_string(message);
			//Print(msg);
		}
		this_thread::sleep_for(1s);
	}

	auto lingerTime = 0;
	res = zmq_setsockopt(socketSender, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(socketSender);
	res = zmq_ctx_destroy(ctx);
}

void Subscriber(string name, string ProxyAddress, int filter)
{
	auto ctx = zmq_ctx_new();
	auto socketReceiver = zmq_socket(ctx, ZMQ_SUB);
	auto res = zmq_connect(socketReceiver, ProxyAddress.c_str());
	res = zmq_setsockopt(socketReceiver, ZMQ_SUBSCRIBE, &filter, sizeof(filter));
	auto timeout = 1000;
	res = zmq_setsockopt(socketReceiver, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));

	int buffer;

	while (kLiveFlag.load())
	{
		res = zmq_recv(socketReceiver, &buffer, sizeof(buffer), 0);
		if (res < 0)
		{
			this_thread::sleep_for(1s);
		}
		else
		{
			auto msg = "Subscriber (" + name + ") receive: " + to_string(buffer);
			Print(msg);
		}
	}

	auto lingerTime = 0;
	res = zmq_setsockopt(socketReceiver, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(socketReceiver);
	res = zmq_ctx_destroy(ctx);
}

int main()
{
	auto publisherPort = 9000;
	auto publisherAddress = "tcp://127.0.0.1:"s + to_string(publisherPort);
	auto pub1 = thread(Publisher, "pub1"s, publisherAddress, 66);

	auto publisherPort2 = 9001;
	auto publisherAddress2 = "tcp://127.0.0.1:"s + to_string(publisherPort2);
	auto pub2 = thread(Publisher, "pub2"s, publisherAddress2, 77);

	auto publisherAddresses = vector<string>{ publisherAddress, publisherAddress2 };

	auto proxyPublisherPort = 10000;
	auto proxyPublisherAddress = "tcp://127.0.0.1:"s + to_string(proxyPublisherPort);
	auto proxy = thread(Proxy, publisherAddresses, proxyPublisherAddress);

	auto sub1 = thread(Subscriber, "sub1"s, proxyPublisherAddress, 66);
	auto sub2 = thread(Subscriber, "sub2"s, proxyPublisherAddress, 77);
	while (kLiveFlag)
	{
		this_thread::sleep_for(1s);
	}
	proxy.join();
	pub1.join();
	pub2.join();
	sub1.join();
	sub2.join();
}
