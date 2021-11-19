#include <map>
#include <cstring>
#include <fstream>
#include <sstream>
#include <iostream>
#include <future>

/* This is just a very simple and inefficient demo of async responses,
 * please do roll your own variant or use a database or Node.js's async
 * features instead of this really bad demo */
struct AsyncFileReader {
private:
    /* The cache we have in memory for this file */
    std::string cache;
    int cacheOffset;
    bool hasCache;

    /* The pending async file read (yes we only support one pending read) */
    std::function<void(std::string_view)> pendingReadCb;

    int fileSize;
    std::string fileName;
    std::ifstream fin;
    uWS::Loop *loop;
	int namepos = -1;
public:
	void log(const char* fmt, ...) {
		va_list vl;
		va_start(vl, fmt);
		char line[1024]; char* p = line;
		p += sprintf(line, "%s> ", &fileName[namepos + 1]);
		p += vsprintf(p, fmt, vl); 
		puts(line);
		va_end(vl);
	}
    /* Construct a demo async. file reader for fileName */
    AsyncFileReader(std::string fileName) : fileName(fileName) {
        fin.open(fileName, std::ios::binary);
		namepos = fileName.rfind('/');
		if(namepos == std::string::npos)
			namepos = fileName.rfind('\\');
        // get fileSize
        fin.seekg(0, fin.end);
        fileSize = fin.tellg();

        log("File size is: %d", fileSize);

        // cache up 1 mb!
        cache.resize(std::min<int>(fileSize, 1024 * 1024));
		log("Caching %d at offset 0", cache.size());

        fin.seekg(0, fin.beg);
        fin.read(cache.data(), cache.length());
        cacheOffset = 0;
        hasCache = true;

        // get loop for thread

        loop = uWS::Loop::get();
    }

    /* Returns any data already cached for this offset */
    std::string_view peek(int offset) {
        /* Did we hit the cache? */
        if (hasCache && offset >= cacheOffset && ((offset - cacheOffset) < cache.length())) {
            /* Cache hit */

            /*if (fileSize - offset < cache.length()) {
                std::cout << "LESS THAN WHAT WE HAVE!" << std::endl;
            }*/

            int chunkSize = std::min<int>(fileSize - offset, cache.length() - offset + cacheOffset);
			log("cache hit %d -> %d", offset, chunkSize);

            return std::string_view(cache.data() + offset - cacheOffset, chunkSize);
        } else {
            /* Cache miss */
			log("Cache %d miss!", offset);
            return std::string_view(nullptr, 0);
        }
    }

    /* Asynchronously request more data at offset */
    void request(int offset, std::function<void(std::string_view)> cb) {

        // in this case, what do we do?
        // we need to queue up this chunk request and callback!
        // if queue is full, either block or close the connection via abort!
        if (!hasCache) {
            // already requesting a chunk!
            log("ERROR: already requesting a chunk!");
            return;
        }

        // disable cache
        hasCache = false;

        std::async(std::launch::async, [this, cb, offset]() {
            log("ASYNC Caching 1 MB at offset=%d fileSize=%d", offset, fileSize);

            // den har stängts! öppna igen!
            if (!fin.good()) {
                fin.close();

                log("Reopening fin!");
                fin.open(fileName, std::ios::binary);
            }
            fin.seekg(offset, fin.beg);
            fin.read(cache.data(), cache.length());

            cacheOffset = offset;

            loop->defer([this, cb, offset]() {

                int chunkSize = std::min<int>(cache.length(), fileSize - offset);

                // båda dessa sker, wtf?
                if (chunkSize == 0) {
                    log("Zero size!?");
                }

                if (chunkSize != cache.length()) {
                    log("LESS THAN A CACHE 1 MB!");
                }

                hasCache = true;
                cb(std::string_view(cache.data(), chunkSize));
            });
        });
    }

    /* Abort any pending async. request */
    void abort() {
    }

    int getFileSize() {
        return fileSize;
    }
};
