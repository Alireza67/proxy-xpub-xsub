#include <zmq.h>
#include <string>
#include <atomic>
#include <thread>
#include <iostream>
#include <mutex>
#include <vector>

using namespace std;

atomic<bool> kLiveFlag = true;
auto liveCouner = 0;
mutex lock_;

void Print(string msg)
{
	unique_lock<mutex>lk(lock_);
	cout << msg << endl;
}

auto ctx = zmq_ctx_new();

void Proxy(vector<string> publisherAddresses, string proxyPublisherAddress)
{
	auto xpub = zmq_socket(ctx, ZMQ_XPUB);
	auto res = zmq_bind(xpub, proxyPublisherAddress.c_str());

	auto xsub = zmq_socket(ctx, ZMQ_XSUB);
	for (auto& item : publisherAddresses)
	{
		res = zmq_connect(xsub, item.c_str());
	}

	res = zmq_proxy(xsub, xpub, NULL);
	cout << "CLOSE PROXY!" << endl;

	while (kLiveFlag)
	{
		this_thread::sleep_for(1s);
	}

	auto lingerTime = 0;
	res = zmq_setsockopt(xsub, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_setsockopt(xpub, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(xsub);
	res = zmq_close(xpub);
}

void Publisher(string name, string address, int filter, int message)
{
	auto socketSender = zmq_socket(ctx, ZMQ_PUB);
	auto res = zmq_bind(socketSender, address.c_str());

	while (kLiveFlag.load())
	{
		res = zmq_send(socketSender, &filter, sizeof(filter), ZMQ_SNDMORE);
		res = zmq_send(socketSender, &message, sizeof(message), 0);
		this_thread::sleep_for(1s);
	}

	auto lingerTime = 0;
	res = zmq_setsockopt(socketSender, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(socketSender);
}

void Subscriber(string name, string ProxyAddress, int filter)
{
	auto socketReceiver = zmq_socket(ctx, ZMQ_SUB);
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
		if (res < 0)
		{
			this_thread::sleep_for(1s);
		}
		else
		{
			auto msg = "Subscriber (" + name + ") receive: key: " + to_string(key) + " value: " + to_string(buffer);
			Print(msg);
		}
	}

	auto lingerTime = 0;
	res = zmq_setsockopt(socketReceiver, ZMQ_LINGER, &lingerTime, sizeof(lingerTime));
	res = zmq_close(socketReceiver);
}

int main()
{
	auto publisherPort = 9000;
	auto publisherAddress = "inproc://job_1";
	auto pub1 = thread(Publisher, "pub1"s, publisherAddress, 66, 30);

	auto publisherPort2 = 9001;
	auto publisherAddress2 = "inproc://job_2";
	auto pub2 = thread(Publisher, "pub2"s, publisherAddress2, 77, 60);

	auto publisherAddresses = vector<string>{ publisherAddress, publisherAddress2 };

	auto proxyPublisherPort = 10000;
	auto proxyPublisherAddress = "tcp://127.0.0.1:"s + to_string(proxyPublisherPort);
	auto proxy = thread(Proxy, publisherAddresses, proxyPublisherAddress);

	auto sub1 = thread(Subscriber, "sub1"s, proxyPublisherAddress, 66);
	auto sub2 = thread(Subscriber, "sub2"s, proxyPublisherAddress, 77);



	while (liveCouner < 600)
	{
		liveCouner++;
		this_thread::sleep_for(1s);
	}
	kLiveFlag.store(false);
	pub1.join();
	pub2.join();
	sub1.join();
	sub2.join();
	auto res = zmq_ctx_shutdown(ctx);
	res = zmq_ctx_destroy(ctx);
	proxy.join();
}
