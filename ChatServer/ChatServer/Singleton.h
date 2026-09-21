#pragma once
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>

template<typename T>
class Singleton
{
public:
	static std::shared_ptr<T> GetInstance() {
		static std::shared_ptr<T> _instance = std::shared_ptr<T>(new T());
		return _instance;
	}

	// A singleton can outlive the logging registry during static destruction.
	~Singleton() = default;
protected:
	Singleton() = default;
	Singleton(const Singleton&) = delete;
	Singleton& operator = (const Singleton&) = delete;
};

