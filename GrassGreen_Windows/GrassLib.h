#ifndef GRASSLIB_H
#define GRASSLIB_H

#include <string>
#include <vector>
#include <map>
#include <windows.h>

// Единая структура элементов для всего движка
struct UIElement {
    std::string type;      
    std::string id;        
    std::wstring text;      
    int x = 0, y = 0; 
    int w = 150, h = 35; 
    int align = 0; 
    COLORREF txt_color = RGB(0, 255, 204); 
    int f_size = 18;
    std::wstring f_style = L"Segoe UI";
    HWND hwnd = NULL;
    
    bool has_run_action = false; 
    std::string run_target;           

    bool is_hovered = false; 
    bool is_placeholder = true; 
};

extern std::map<std::string, std::wstring> variables_cache;

void GrassLib_Load(const std::string& lib_name);
void GrassStorageEngine_Load(const std::string& filename);

#endif // GRASSLIB_H