/*
 * pdf-poppler.cpp
 *
 *  Created on: Oct 31, 2017
 *      Author: gnat
 */

#include <phpcpp.h>
#include <iostream>
#include <string.h>
#include <sys/stat.h>
#include <libgen.h>
#include "pdf-document.h"
#include "pdf-image-result.h"
#include "pdf-image-format.h"
#include <limits.h>
#include <poppler-page.h>
#include <openssl/sha.h>

static int _mkdir(const char *dir) {
    struct stat buffer;
    char tmp[256];
    char *p = NULL;
    int status;
    size_t len;

    snprintf(tmp, sizeof(tmp),"%s",dir);
    len = strlen(tmp);

    if (tmp[len - 1] == '/') {
        tmp[len - 1] = 0;
    }

    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
	    if (stat(tmp, &buffer) == -1) {
                status = mkdir(tmp, S_IRWXU);
                if (status != 0) {
                    Php::error << "PHP-PDF: Unable to create dir " << tmp << " because '" << strerror(errno) << "'" << std::endl;
                    return -1;
                }
	    }
            *p = '/';
        }
    }

    mkdir(tmp, S_IRWXU);
    return 0;
}

PdfDocument::PdfDocument() {
    jpeg = new PdfImageFormat("jpeg", "jpg");
    png = new PdfImageFormat("png", "png");
    tiff = new PdfImageFormat("tiff", "tif");
}

void PdfDocument::__construct(Php::Parameters &params) {
    _document = poppler::document::load_from_file(params[0]); //,params[1],params[2]);
}

Php::Value PdfDocument::getCreator(){
    poppler::byte_array arr;
    char *c_str;

    arr = _document->info_key("Creator").to_utf8();
    c_str = &arr[0];
    return c_str;
}

Php::Value PdfDocument::getCreationDate(){
    char buffer[50];
    poppler::time_type t = _document->info_date("CreationDate");
    if(t == UINT_MAX) {
        return nullptr;
    }

    snprintf(buffer,50,"@%u",t);
    return Php::Object("DateTime", buffer);
}

Php::Value PdfDocument::getModifiedDate() {
    char buffer[50];
    poppler::time_type t = _document->info_date("ModDate");
    if(t == UINT_MAX) {
        return nullptr;
    }

    snprintf(buffer,50,"@%u",t);
    return Php::Object("DateTime", buffer);
}

Php::Value PdfDocument::getMajorVersion() {
    if (_major == 0) {
        _document->get_pdf_version(&_major, &_minor);
    }

    return _major;
}

Php::Value PdfDocument::getMinorVersion() {
    if (_major == 0) {
        _document->get_pdf_version(&_major, &_minor);
    }

    return _minor;
}

Php::Value PdfDocument::hasEmbeddedFiles() {
    return _document->has_embedded_files();
}

Php::Value PdfDocument::isEncrypted() {
    return _document->is_encrypted();
}

Php::Value PdfDocument::isLinear() {
    return _document->is_linearized();
}

Php::Value PdfDocument::isLocked() {
    return _document->is_locked();
}

Php::Value PdfDocument::numberOfPages() {
    return _document->pages();
}

Php::Value PdfDocument::asString() {
    int firstPage;
    int lastPage;
    int x;
    poppler::page *page;
    poppler::ustring pageData;
    std::string resultData;
    poppler::byte_array arr;
    char *c_str;

    firstPage = 0;
    lastPage = _document->pages();

    for (x = firstPage; x < lastPage; x++) {
        page = _document->create_page(x);
        pageData = page->text(page->page_rect(poppler::media_box));
        arr = pageData.to_utf8();
        c_str = &arr[0];
        resultData.append(std::string(c_str, arr.size()));
    }

    return resultData;
}

Php::Value PdfDocument::toImage(Php::Parameters &params) {
    int firstPage = 0;
    int lastPage = _document->pages();
    int resolution = 75;
    int x;
    struct stat buffer;
    Php::Value returnValue;

    char pattern[300];
    PdfImageFormat * format = NULL;
    poppler::page *page;
    poppler::page_renderer *renderer;
    poppler::image image;

    format = this->getImageFormat(params[0]);
    if (format == NULL) {
        Php::error << "Unable to determine type" << std::endl;
        return false;
    }

    if (params[1].size() + 10 > 255) {
        Php::error << "Path is larger than 255 chars - Unable to proceed" << std::endl;
        return false;
    }

    if (params.size() == 3) {
        resolution = params[2];
    }

    renderer = new poppler::page_renderer();

    params[1].stringValue().copy(pattern,params[1].size(),0);

    char * _dirname = dirname(pattern);
    Php::out << "Attempting to create dir: " << _dirname << std::endl;
    // Check that the directory exists
    if (stat (_dirname, &buffer) == -1) {
        if (_mkdir(_dirname) != 0) {
            Php::error << "PHP-PDF: Unable to create dir " << _dirname << " because '" << strerror(errno) << "'" << std::endl;
            throw Php::Exception("Unable to create dir");
        }
    }

    for (x = firstPage; x < lastPage; x++) {
        int imageWidth = 0;
        int imageHeight = 0;
        int pageWidth = 0;
        int pageHeight = 0;
        char outFile[255];

        memset(&outFile, 0, 255);
        sprintf(&outFile[0], "%s-%d.%s", params[1].stringValue().c_str(), x, format->getExtension());

        page = _document->create_page(x);

        poppler::rectf pageDim = page->page_rect();
        pageWidth = pageDim.width();
        pageHeight = pageDim.height();

        image = renderer->render_page(page, resolution, resolution, 0, 0, -1, -1, poppler::rotation_enum::rotate_0);
        image.save(outFile, format->getFormat(), resolution);

        imageWidth = image.width();
        imageHeight = image.height();

        returnValue[x] = Php::Object("\\PDF\\PdfImageResult", new PdfImageResult(pageWidth, pageHeight, imageWidth, imageHeight, outFile));
    }

    return returnValue;
}

Php::Value PdfDocument::hash(Php::Parameters &params) {
    char mdString[SHA_DIGEST_LENGTH*2+1];
    unsigned char md[SHA_DIGEST_LENGTH];

    poppler::page *page;
    poppler::ustring pageData;
    poppler::byte_array arr;

    int lastPage = _document->pages();

    SHA_CTX context;
    if (!SHA1_Init(&context)) {
        Php::error << "Unable to initialize openssl context" << std::endl;
        return -1;
    }

    for (int x = 0; x < lastPage; x++) {
        page = _document->create_page(x);
        pageData = page->text(page->page_rect(poppler::media_box));
        arr =  pageData.to_utf8();
        if (!SHA1_Update(&context, (unsigned char*)&arr[0], arr.size())) {
	    Php::error << "Unable to add data to hash context" << std::endl;
	    return -1;
        }
    }

    if (!SHA1_Final(md,&context)) {
        Php::error << "Unable to finalize hash" << std::endl;
        return -1;
    }

    for (int i = 0; i < SHA_DIGEST_LENGTH; i++) {
       sprintf(&mdString[i*2], "%02x", (unsigned int)md[i]);
    }

    return mdString;
}

Php::Value PdfDocument::compare(Php::Parameters &params) {
    PdfDocument *document = (PdfDocument *)params[0].implementation();

    poppler::page *localPage;
    poppler::page *externalPage;
    poppler::ustring localPageData;
    poppler::ustring externalPageData;

    poppler::byte_array localPageText_arr;
    poppler::byte_array externalPageText_arr;

    char *localPageText_str;
    char *externalPageText_str;

    int textDifference = 0;

    int count = _document->pages();
    if (count != document->_document->pages()) {
        return 10;
    }

    for (int pageNum = 0; pageNum < count; ++pageNum) {
        localPage = _document->create_page(pageNum);
        if (!localPage) {
            Php::error << "PHP-PDF: Failed to read local page" << pageNum+1 << std::endl;

            return -1;
        }

        externalPage = document->_document->create_page(pageNum);
        if (!externalPage) {
            Php::error << "PHP-PDF: Failed to read " << pageNum+1 << std::endl;

            return -1;
        }

        localPageData = localPage->text();
        externalPageData = externalPage->text();

        if (localPageData.length() != externalPageData.length()) {
            return 12;
        }

        localPageText_arr = localPageData.to_utf8();
        localPageText_str = &localPageText_arr[0];

        externalPageText_arr = externalPageData.to_utf8();
        externalPageText_str = &externalPageText_arr[0];

        textDifference = std::memcmp(localPageText_str, externalPageText_str, localPageData.length());
        if (textDifference != 0) {
            return 13;
        }
    }

    return 0;
}

PdfImageFormat * PdfDocument::getImageFormat(int inFormat) {
    switch (inFormat) {
        case 2:
            return png;
            break;
        case 3:
            return tiff;
        default:
        case 1:
            return jpeg;
    }

    return NULL;
}

