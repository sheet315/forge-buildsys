#include <fstream>
#include <iostream>
#include <filesystem>
#include <string>
#include <vector>
#include <regex>
#include <utility>
#include <chrono>

bool done = false;
bool depchanged = false;

struct Var {
    std::string name;
    std::string value;

    Var() = default;

    Var(const std::string& n, const std::string& v) : name(n), value(v) {}
};

struct Global {
    std::vector<Var> vars;

    Global() = default;

    Global(const std::vector<Var>& v) : vars(v) {}
};

struct Function {
    std::string name;
    std::string target = ".NONE";
    std::string source = ".NONE";
    std::vector<std::string> commands;
    std::vector<std::string> dependencies;
    std::vector<Var> vars;
    bool rerun = false;
    int minIdx = 0;

    Function() = default;

    Function(const std::string& n, const std::vector<std::string>& cmds, const std::vector<Var>& v) : name(n), commands(cmds), vars(v) {}
};

std::string color(int color, const std::string& text, bool bold = false) {
    int r = (color >> 16) & 0xFF;
    int g = (color >> 8) & 0xFF;
    int b = color & 0xFF;

    std::ostringstream out;

    out << "\033[";

    if (bold) {
        out << "1;";
    }

    out << "38;2;"
        << r << ";"
        << g << ";"
        << b << "m"
        << text
        << "\033[0m";

    return out.str();
}

std::vector<std::pair<std::string, size_t>> extractBetweenWithPos(const std::string& input, const std::string& openDelim, const std::string& closeDelim) {
    std::vector<std::pair<std::string, size_t>> result;

    size_t pos = 0;

    while (true) {
        size_t start = input.find(openDelim, pos);
        if (start == std::string::npos) break;

        size_t contentStart = start + openDelim.size();

        size_t end = input.find(closeDelim, contentStart);
        if (end == std::string::npos) break;

        std::string extracted = input.substr(contentStart, end - contentStart);

        result.emplace_back(extracted, contentStart);

        pos = end + closeDelim.size();
    }

    return result;
}

std::vector<std::string> splitString(const std::string& input, const std::string& delimiter, int count = -1) {
    std::vector<std::string> result;
    size_t pos = 0;
    int splits = 0;
    while (true) {
        if (count != -1 && splits >= count) {
            result.push_back(input.substr(pos));
            break;
        }
        size_t found = input.find(delimiter, pos);
        if (found == std::string::npos) {
            result.push_back(input.substr(pos));
            break;
        }
        result.push_back(input.substr(pos, found - pos));
        pos = found + delimiter.size();
        splits++;
    }
    return result;
}

int findIndex(const std::vector<std::string>& vec, const std::string& target) {
    for (int i = 0; i < static_cast<int>(vec.size()); i++) {
        if (vec[i] == target) {
            return i;
        }
    }
    return -1;
}

int findFunctionIndex(const std::vector<Function>& functions, const std::string& name) {
    for (size_t i = 0; i < functions.size(); i++) {
        if (functions[i].name == name) return i;
    }
    return -1;
}

std::vector<std::string> parseList(const std::string& s) {
    std::vector<std::string> result;

    if (s.size() < 2 || s.front() != '[' || s.back() != ']')
        return result;

    std::string content = s.substr(1, s.size() - 2);

    std::stringstream ss(content);
    std::string item;

    while (std::getline(ss, item, ',')) {
        result.push_back(item);
    }

    return result;
}

bool needsRerun(const std::filesystem::path& source, const std::filesystem::path& target, bool force) {
    if (force) return true;
    namespace fs = std::filesystem;

    if (!fs::exists(target)) return true;

    return fs::last_write_time(source) > fs::last_write_time(target);
}

std::string getTargets(std::vector<Function> &functions, Function &func, bool first = true) {
    if (func.dependencies.size() == 0) {
        return func.target + " ";
    }
    std::string targets;
    for (auto d : func.dependencies) {
        int idx = findFunctionIndex(functions, d);
        if (idx == -1) {
            std::cout << color(0xff0000, "*!Forge error!", true) << " Dependency missing: " << d << '\n';
            exit(1);
        }
        targets += getTargets(functions, functions[idx], false) + std::string(first ? "" : func.target + " ");
    }
    return targets;
}

void execFunc(int argc, std::vector<std::string> &args, std::vector<Function> &functions, Function& func, Function* parent = nullptr, bool first = true, bool dep = false) {
    bool run = needsRerun(func.source, func.target, func.rerun);
    if (dep && run) {
        std::cout << color(0x1e90ff, " [Running dependency] " + func.name  + '\n', true);
    }
    if (func.dependencies.size() > 0 && (first || run)) {
        std::string value = "";
        for (auto d : func.dependencies) {
            int idx = findFunctionIndex(functions, d);
            if (idx == -1) {
                std::cout << color(0xff0000, "*!Forge error!", true) << " Dependency missing: " << d << '\n';
                exit(1);
            }

            execFunc(argc, args, functions, functions[idx], &func, false, true);
        }
    }

    if (run || (first && depchanged)) {
        if (argc < func.minIdx + 1) {
            std::cout << color(0xff0000, "*!Forge error!", true) << " Insufficient arguments.\n Function " << func.name << " requires " << func.minIdx << " arguments." << '\n';
            exit(1);
        } else {
            if (first) std::cout << color(0x1e90ff, " [Running] " + args[1]  + '\n', true);
            for (auto c : func.commands) {
                if (c.find("[DEPTARGETS]") != std::string::npos) {
                    c.replace(c.find("[DEPTARGETS]"), 12, getTargets(functions, func));
                }
                std::cout << color(0x1e90ff, " | ", true) << c << '\n';
                std::system(c.c_str());
                if (dep && parent) {
                    depchanged = true;
                    parent->rerun = true;
                }
                done = true;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    std::filesystem::path buildfile = "Buildfile";
    if (!std::filesystem::exists(buildfile)) {
        std::cout << color(0xff0000, "*!Forge error!", true) << " No Buildfile in current directory.\n Please create a Buildfile for this project.\n";
        return 1;
    }
    if (argc <= 1) {
        std::cout << color(0xff0000, "*!Forge error!", true) << " No function has been selected.\n Please run this command with a function: '" << argv[0] << " " << "example'\n";
        return 1;
    }

    std::vector<std::string> args;
    for (size_t i = 0; i < argc; i++) {
        args.emplace_back(argv[i]);
    }

    std::vector<Function> functions;

    std::ifstream in(buildfile);
    std::string line;

    bool inFunction = false;

    Global global;
    Function currentFunction;
    
    while (std::getline(in, line)) {
        if (line.find("=") != std::string::npos) {
            std::vector<std::string> sides = splitString(line, "=", 1);
            std::string name = sides[0];
            name.erase(0, name.find_first_not_of(" \t\n\r\f\v"));

            while (extractBetweenWithPos(sides[1], "arg[", "]").size() >= 1) {
                std::pair<std::string, size_t> v = extractBetweenWithPos(sides[1], "arg[", "]")[0];
                int idx;
                try {
                    idx = std::stoi(v.first);
                } catch (const std::invalid_argument& e) {
                    std::cout << color(0xff0000, "*!Forge error!", true) << " Invalid integer in arg[N] usage.\n";
                    return 1;
                }
                catch (const std::out_of_range& e) {
                    std::cout << color(0xff0000, "*!Forge error!", true) << " Invalid integer in arg[N] usage.\n";
                    return 1;
                }
                if (idx > currentFunction.minIdx) {
                    currentFunction.minIdx = idx;
                }
                std::string value = argc >= idx + 1 ? args[idx] : "NONE";
                sides[1].replace(v.second - 4, v.first.length()+5, value);
            }

            if (inFunction) {        
                if (name == "DEPEND" && sides[1].find("[") != std::string::npos && sides[1].find("]") != std::string::npos && sides[1].find("arg") == std::string::npos) {
                    currentFunction.dependencies = parseList(sides[1]);
                } else if (name == "TARGET") {
                    currentFunction.target = sides[1];
                } else if (name == "SOURCE") {
                    currentFunction.source = sides[1];
                } else if (name == "FORCERUN") {
                    currentFunction.rerun = sides[1] == "true" ? true : false;
                }

                currentFunction.vars.emplace_back(name, sides[1]);
            } else {
                global.vars.emplace_back(name, sides[1]);
            }
            
        } else if (line.find("{") != std::string::npos && line.find("}") == std::string::npos) {
            if (!inFunction) {
                currentFunction.name = splitString(line, " {")[0];
                inFunction = true;
            }
        } else if (line.find("}") != std::string::npos && line.find("{") == std::string::npos) {
            if (inFunction) {
                inFunction = false;
                functions.emplace_back(currentFunction);
                currentFunction = Function();
            }
        } else if (!line.empty() && !std::all_of(line.begin(), line.end(), [](unsigned char c){ return std::isspace(c); })) {
            line.erase(0, line.find_first_not_of(" \t\n\r\f\v"));
            currentFunction.commands.emplace_back(line);
        }
    }
    if (inFunction) {
        std::cout << color(0xff0000, "*!Forge error!", true) << " Function missing closing brace.\n";
        return 1;
    }

    std::vector<std::string> globalVarNames;
    std::vector<std::string> globalVarValues;

    for (auto v : global.vars) {
        globalVarNames.emplace_back(v.name);
        globalVarValues.emplace_back(v.value);
    }
    for (auto &f : functions) {
        std::vector<std::string> varNames;
        std::vector<std::string> varValues;

        for (auto v : f.vars) {
            varNames.emplace_back(v.name);
            varValues.emplace_back(v.value);
        }
        for (size_t i = 0; i < f.commands.size(); i++) {
            while (extractBetweenWithPos(f.commands[i], "{", "}").size() >= 1) {
                std::pair<std::string, size_t> v = extractBetweenWithPos(f.commands[i], "{", "}")[0];
                std::string value;
                int idx = findIndex(varNames, v.first);
                if (idx == -1) {
                    idx = findIndex(globalVarNames, v.first);
                    if (idx != -1) {
                       value = globalVarValues[idx]; 
                    }
                } else {
                    value = varValues[idx];
                }
                if (idx == -1) {
                    std::cout << color(0xff0000, "*!Forge error!", true) << " Undefined variable.\n Variable: " << color(0xff0000, v.first) << "\n";
                    return 1;
                }

                f.commands[i].replace(v.second - 1, v.first.length()+2, value);
            }
        }
    }
    int idx = findFunctionIndex(functions, args[1]);
    if (idx == -1) {
        std::cout << color(0xff0000, "*!Forge error!", true) << " Function " << args[1] << " not found.\n Available functions: ";
        for (auto f : functions) {
            std::cout << f.name << " ";
        }
        std::cout << '\n';
        return 1;
    }
    execFunc(argc, args, functions, functions[idx]);
    if (!done) {
        std::cout << color(0x1e90ff, " [Nothing to be done]\n", true);
        return 0;
    }

    std::cout << color(0x1e90ff, " [Run complete] " + args[1]  + '\n', true);
    return 0;
}
