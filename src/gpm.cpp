#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#include <direct.h>
#define GRG_MKDIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define GRG_MKDIR(p) mkdir(p, 0755)
#endif

using namespace std;

static bool make_directory(const string& path) {
    return GRG_MKDIR(path.c_str()) == 0;
}

static bool write_text_file(const string& path, const string& content) {
    ofstream ofs(path);
    if (!ofs.is_open()) return false;
    ofs << content;
    return true;
}

static string read_text_file(const string& path) {
    ifstream ifs(path);
    if (!ifs.is_open()) return "";
    stringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static void print_help() {
    cout << "Grass Package Manager (gpm) v1.0\n"
         << "Usage:\n"
         << "  gpm init <project_name>      Initialize a new GrassGreen project\n"
         << "  gpm build                    Build project according to grass.cfg\n"
         << "  gpm run                      Build and run project\n"
         << "  gpm create <lib_name>        Create a new library template\n"
         << "  gpm publish-index <dir>      Generate index.json for package registry\n"
         << "  gpm install <lib_name>       Download and install a library\n";
}

static int cmd_init(const string& proj_name) {
    if (proj_name.empty()) {
        cerr << "[gpm] Error: Project name is required.\n";
        return 1;
    }
    make_directory(proj_name);
    make_directory(proj_name + "/src");
    make_directory(proj_name + "/libs");
    make_directory(proj_name + "/build");

    string cfg = "[project]\n"
                 "name = \"" + proj_name + "\"\n"
                 "version = \"0.1.0\"\n"
                 "entry = \"src/main.grass\"\n"
                 "out = \"build/" + proj_name + 
#if defined(_WIN32)
                 ".exe\"\n\n"
#else
                 "\"\n\n"
#endif
                 "[deps]\n";

    write_text_file(proj_name + "/grass.cfg", cfg);

    string main_code = "!GrassGreen!\n\n"
                       "int main() {\n"
                       "    gout << \"Hello from " + proj_name + "!\" << endl;\n"
                       "    return 0;\n"
                       "}\n";
    write_text_file(proj_name + "/src/main.grass", main_code);

    cout << "[gpm] Project '" << proj_name << "' initialized successfully.\n";
    return 0;
}

static int cmd_build_or_run(bool do_run) {
    string cfg = read_text_file("grass.cfg");
    if (cfg.empty()) {
        cerr << "[gpm] Error: grass.cfg not found in current directory.\n";
        return 1;
    }

    string entry = "src/main.grass";
    string out_file = "build/app";
    string line;
    stringstream ss(cfg);
    while (getline(ss, line)) {
        size_t eq = line.find('=');
        if (eq != string::npos) {
            string key = line.substr(0, eq);
            string val = line.substr(eq + 1);
            while (!key.empty() && (key.back() == ' ' || key.back() == '\t')) key.pop_back();
            while (!key.empty() && (key.front() == ' ' || key.front() == '\t')) key.erase(key.begin());
            while (!val.empty() && (val.front() == ' ' || val.front() == '\t' || val.front() == '\"')) val.erase(val.begin());
            while (!val.empty() && (val.back() == ' ' || val.back() == '\t' || val.back() == '\"' || val.back() == '\r')) val.pop_back();

            if (key == "entry") entry = val;
            else if (key == "out") out_file = val;
        }
    }

    make_directory("build");

#if defined(_WIN32)
    string compiler = "grg.exe";
#else
    string compiler = "grg";
#endif

    string cmd;
    if (do_run) {
        cmd = compiler + " -r " + entry;
    } else {
        cmd = compiler + " -emit-exe " + entry + " -o " + out_file;
    }

    cout << "[gpm] Executing: " << cmd << "\n";
    int ret = system(cmd.c_str());
    return ret;
}

static int cmd_create(const string& lib_name) {
    if (lib_name.empty()) {
        cerr << "[gpm] Error: Library name is required.\n";
        return 1;
    }

    make_directory(lib_name);

    string pkg = "{\n"
                 "  \"name\": \"" + lib_name + "\",\n"
                 "  \"version\": \"1.0.0\",\n"
                 "  \"description\": \"GrassGreen library " + lib_name + "\",\n"
                 "  \"author\": \"Community\",\n"
                 "  \"entry\": \"" + lib_name + ".grass\"\n"
                 "}\n";
    write_text_file(lib_name + "/package.json", pkg);

    string code = "!GrassGreen!\n\n"
                  "// Library " + lib_name + "\n"
                  "take " + lib_name + "_hello() {\n"
                  "    return \"Hello from " + lib_name + "\";\n"
                  "}\n";
    write_text_file(lib_name + "/" + lib_name + ".grass", code);

    string readme = "# " + lib_name + "\n\nGrassGreen library.\n";
    write_text_file(lib_name + "/README.md", readme);

    cout << "[gpm] Created library template in '" << lib_name << "/'.\n";
    return 0;
}

static int cmd_publish_index(const string& pkg_dir) {
    string dir = pkg_dir.empty() ? "." : pkg_dir;
    string index_path = dir + "/index.json";

    string json = "{\n  \"registry\": \"GrassGreen Official Registry\",\n  \"packages\": [\n";
    json += "    {\n      \"name\": \"disp\",\n      \"version\": \"1.0.0\",\n      \"file\": \"disp.grass\"\n    }\n";
    json += "  ]\n}\n";

    write_text_file(index_path, json);
    cout << "[gpm] Generated package index at: " << index_path << "\n";
    return 0;
}

static int cmd_install(const string& lib_name) {
    if (lib_name.empty()) {
        cerr << "[gpm] Error: Library name is required.\n";
        return 1;
    }

    make_directory("libs");
    string target_path = "libs/" + lib_name + ".grass";

    // Download or create library placeholder
    string content = read_text_file(target_path);
    if (content.empty()) {
#if defined(_WIN32)
        string curl_cmd = "curl -s -L \"https://grassgreen.info/packages/" + lib_name + ".grass\" -o " + target_path;
#else
        string curl_cmd = "curl -s -L \"https://grassgreen.info/packages/" + lib_name + ".grass\" -o " + target_path;
#endif
        int res = system(curl_cmd.c_str());
        content = read_text_file(target_path);
        if (content.empty() || res != 0) {
            string stub = "!GrassGreen!\n// Package: " + lib_name + "\n";
            write_text_file(target_path, stub);
        }
    }

    // Append to grass.cfg if present
    string cfg = read_text_file("grass.cfg");
    if (!cfg.empty() && cfg.find(lib_name) == string::npos) {
        ofstream ofs("grass.cfg", ios::app);
        ofs << lib_name << " = \"latest\"\n";
    }

    cout << "[gpm] Package '" << lib_name << "' installed into " << target_path << "\n";
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    string command = argv[1];
    if (command == "init") {
        return cmd_init(argc >= 3 ? argv[2] : "");
    } else if (command == "build") {
        return cmd_build_or_run(false);
    } else if (command == "run") {
        return cmd_build_or_run(true);
    } else if (command == "create") {
        return cmd_create(argc >= 3 ? argv[2] : "");
    } else if (command == "publish-index") {
        return cmd_publish_index(argc >= 3 ? argv[2] : ".");
    } else if (command == "install") {
        return cmd_install(argc >= 3 ? argv[2] : "");
    } else {
        cerr << "[gpm] Unknown command: " << command << "\n";
        print_help();
        return 1;
    }
}
