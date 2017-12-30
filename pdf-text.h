/*
 * pdf-text.h
 *
 *  Created on: Dec 29, 2017
 *      Author: gnat
 */

#ifndef PDF_TEXT_H_
#define PDF_TEXT_H_

#include <phpcpp.h>

class PdfText : public Php::Base {
private:
    int64_t x;
    int64_t y;
    int64_t fontSize = 10;
    std::string text;
    std::string font;
public:
    PdfText();
    void __construct(Php::Parameters &params);
    Php::Value getX();
    Php::Value getY();
    Php::Value getText();
    Php::Value getFontSize();
    Php::Value getFont();
};

#endif /* PDF_TEXT_H_ */
