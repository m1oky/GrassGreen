#ifndef GRASSWEB_H
#define GRASSWEB_H

#include <string>

// Компиляция .grass файла в чистый HTML/CSS/JS код
std::string CompileGrassToHTML(const std::string& filename);

// Запуск локального веб-сервера и открытие страницы (!GREEN_WWW1! / !GrassWeb!)
void GrassWeb_LoadPage(const std::string& filename);

#endif // GRASSWEB_H