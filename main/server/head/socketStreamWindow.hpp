#include <mutex>
#include "socketStream.hpp"

class SocketStreamWindow
{
public:
	void setIndex(size_t index);
	size_t getIndex();

	bool isAviliable();
	bool check();

	bool enable();
	void disable();

	bool setSocket(Socket socket);
	IOSocketStream& getSocketStream();
	bool closeSocket(bool blocked = true);

	static size_t getEnabledCount();

private:
	static std::mutex sharedMutex;
	static size_t enabledCount;

	size_t index = 0;
	bool aviliable = true;
	IOSocketStream socketStream;
	std::mutex mutex;

	void closeSelf();
};
