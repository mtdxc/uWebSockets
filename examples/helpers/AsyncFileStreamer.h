#include <filesystem>
//根据扩展名(不区分大小写)，返回文件类型 content_type
const char* getContextType(std::string_view url) {
	static std::map<std::string_view, std::string> mimeMap;
	if (mimeMap.empty()) {
		mimeMap["html"] = "text/html";
		mimeMap["htm"] = "text/html";
		mimeMap["css"] = "text/css";
		mimeMap["log"] = "text/plain";
		mimeMap["txt"] = "text/plain";
		mimeMap["ini"] = "text/plain";
		mimeMap["conf"] = "text/plain";
		mimeMap["config"] = "text/plain";
		mimeMap["cfg"] = "text/plain";
		mimeMap["sh"] = "text/plain";
		mimeMap["bat"] = "text/plain";

		mimeMap["jpg"] = "image/jpeg";
		mimeMap["jpeg"] = "image/jpeg";
		mimeMap["png"] = "image/png";
		mimeMap["gif"] = "image/gif";
		mimeMap["ico"] = "image/ico";
		mimeMap["svg"] = "image/svg-xml";

		mimeMap["js"] = "application/javascript";
		mimeMap["xml"] = "application/xml";
		mimeMap["json"] = "application/json";
		mimeMap["xhtml"] = "application/xhtml+xml";
		mimeMap["swf"] = "application/x-shockwave-flash";

		mimeMap["wav"] = "audio/wav";
		mimeMap["mid"] = "audio/midi";
		mimeMap["midi"] = "audio/midi";
		mimeMap["mp3"] = "audio/mp3";
		mimeMap["3gp"] = "audio/3gpp";
		mimeMap["wma"] = "audio/x-ms-wma";

		mimeMap["avi"] = "video/x-msvideo";
		mimeMap["mkv"] = "video/x-matroska";
		mimeMap["mp4"] = "video/mp4";
		mimeMap["rmvb"] = "video/vnd.rn-realvideo";
		mimeMap["flv"] = "video/x-flv"; // "flv-application/octet-stream";

		mimeMap["apk"] = "application/vnd.android.package-archive";
	}

	auto pos = url.rfind('.');
	if (pos != -1) {
		std::string_view ext = url.substr(pos + 1);
		auto it = mimeMap.find(ext);
		if (it != mimeMap.end())
			return it->second.c_str();
	}
	return "application/octet-stream";
}

struct AsyncFileStreamer {

    std::map<std::string_view, AsyncFileReader *> asyncFileReaders;
    std::string root;

    AsyncFileStreamer(std::string root) : root(root) {
        // for all files in this path, init the map of AsyncFileReaders
        // updateRootCache();
    }

    void updateRootCache() {
        // todo: if the root folder changes, we want to reload the cache
        for(auto &p : std::filesystem::recursive_directory_iterator(root)) {
            std::string url = p.path().string().substr(root.length());
            if (url == "/index.html") {
                url = "/";
            }

            char *key = new char[url.length()];
            memcpy(key, url.data(), url.length());
			for (int i =0; i<url.length(); i++)
			{
				if (key[i] == '\\')
					key[i] = '/';
			}
            asyncFileReaders[std::string_view(key, url.length())] = new AsyncFileReader(p.path().string());
        }
    }

    template <bool SSL>
    void streamFile(uWS::HttpResponse<SSL> *res, std::string_view url) {
        auto it = asyncFileReaders.find(url);
        if (it == asyncFileReaders.end()) {
            std::cout << "Did not find file: " << url << std::endl;
			res->end("no found");
        } else {
            streamFile(res, it->second, 0, it->second->getFileSize());
        }
    }

	template <bool SSL>
	void streamFile(uWS::HttpResponse<SSL> *res, uWS::HttpRequest* req) {
		std::string_view url = req->getUrl();
		auto it = asyncFileReaders.find(url);
		AsyncFileReader *file = nullptr;
		if (it == asyncFileReaders.end()) {
			std::string path = root;
			path.append(url.data(), url.length());
			if (std::filesystem::exists(path)) {
				file = new AsyncFileReader(path);
				asyncFileReaders[url] = file;
			}
		}
		else {
			file = it->second;
		}

		if(file){
			int64_t start = 0, end = file->getFileSize();
			// Range: bytes=0-
			auto range = req->getHeader("range");
			if (range.length()) {
				sscanf(range.data(), "bytes=%lld-%lld", &start, &end);
				std::cout << "range:" << start << "->" << end << std::endl;
				res->writeStatus("206 Partial Content");
				// Content-Range: bytes 0-2839222/2839223
				char line[100];
				sprintf(line, "bytes %lld-%lld/%d", start, end - 1, file->getFileSize());
				file->log("Content-Range:%s", line);
				res->writeHeader("Content-Range", line);
			}
			else
				res->writeStatus(uWS::HTTP_200_OK);
			// chrome浏览器只有设了video/mp4的mimeType, 才能直接播放
			res->writeHeader("Content-Type", getContextType(url));
			// 只有响应这头部文件才能拖动播放
			res->writeHeader("Accept-Ranges", "bytes");
			streamFile(res, file, start, end - start);
		}
		else {
			std::cout << "Did not find file: " << url << std::endl;
			res->writeStatus("404 not found");
			res->end("file no found");
		}
	}

    template <bool SSL>
    static void streamFile(uWS::HttpResponse<SSL> *res, AsyncFileReader *asyncFileReader, int64_t start, int64_t size) {
        /* Peek from cache */
        std::string_view chunk = asyncFileReader->peek(res->getWriteOffset() + start);
		res->onAborted([] {
			std::cout << "ABORTED!" << std::endl;
		});
        if (!chunk.length() || res->tryEnd(chunk, size).first) {
            /* Request new chunk */
            // todo: we need to abort this callback if peer closed!
            // this also means Loop::defer needs to support aborting (functions should embedd an atomic boolean abort or something)

            // Loop::defer(f) -> integer
            // Loop::abort(integer)

            // hmm? no?

            // us_socket_up_ref eftersom vi delar ägandeskapet

            if (// chunk.length()
				res->getWriteOffset() < size) {
                asyncFileReader->request(res->getWriteOffset() + start, [=](std::string_view chunk) {
                    // check if we were closed in the mean time
                    //if (us_socket_is_closed()) {
                        // free it here
                        //return;
                    //}

                    /* We were aborted for some reason */
                    if (!chunk.length()) {
                        // todo: make sure to check for is_closed internally after all callbacks!
                        res->close();
                    } else {
                        AsyncFileStreamer::streamFile(res, asyncFileReader, start, size);
                    }
                });
            }
        } else {

            /* We failed writing everything, so let's continue when we can */
            res->onWritable([=](int offset) {

                // här kan skiten avbrytas!

                AsyncFileStreamer::streamFile(res, asyncFileReader, start, size);
                // todo: I don't really know what this is supposed to mean?
                return false;
            });
        }
    }
};
