#pragma once
#include <iostream>
#include <memory>

template<typename T>
class Singleton
{
public:
	static std::shared_ptr<T> GetInstance() {
		static std::shared_ptr<T> _instance = std::shared_ptr<T>(new T());
		return _instance;
	}

	~Singleton() {
		std::cout << "Singleton destructed." << std::endl;
	}
protected:
	Singleton() = default;
	Singleton(const Singleton&) = delete;
	Singleton& operator = (const Singleton&) = delete;
};

