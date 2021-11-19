/* Middleware to fill out content-type */
inline bool hasExt(std::string_view file, std::string_view ext) {
    if (ext.size() > file.size()) {
        return false;
    }
    return std::equal(ext.rbegin(), ext.rend(), file.rbegin());
}

/* This should be a filter / middleware like app.use(handler) */
template <bool SSL>
uWS::HttpResponse<SSL> *serveFile(uWS::HttpResponse<SSL> *res, uWS::HttpRequest *req) {
	// Range: bytes=0-
	auto range = req->getHeader("range");
	if (range.length()) {
		int64_t start = 0, end = 0;
		sscanf(range.data(), "bytes=%lld-%lld", &start, &end);
		std::cout << "range:" << start << "->" << end << std::endl;
		res->setWriteOffset(start);
		res->writeStatus("206 Partial Content");
		// Content-Range: bytes 0-2839222/2839223
		// res->writeHeader("Content-Range", "bytes %lld-", start);
	}
	else
		res->writeStatus(uWS::HTTP_200_OK);

    if (hasExt(req->getUrl(), ".svg")) {
        res->writeHeader("Content-Type", "image/svg+xml");
    }
	// 只有响应这头部才能拖
	res->writeHeader("Accept-Ranges", "bytes");
    return res;
}
