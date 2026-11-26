#include "frontend/GrassSema.h"
#include <algorithm>

namespace grass::sema {

GrassSema::GrassSema() {
    registerBuiltins();
}

void GrassSema::registerBuiltins() {
    registerBuiltinsCore();
    registerBuiltinsMedia();
    registerBuiltinsSys();
}

void GrassSema::enterScope(Scope::Kind kind) {
    scope_pool_.push_back(std::make_unique<Scope>(kind, current_scope_));
    current_scope_ = scope_pool_.back().get();
}

void GrassSema::exitScope() {
    if (current_scope_) {
        current_scope_ = current_scope_->parent;
    }
}

void GrassSema::reportError(const SourceLocation& loc, const std::string& msg, const std::string& hint) {
    diagnostics_.emplace_back(DiagnosticSeverity::Error, loc, msg, hint);
}

void GrassSema::reportWarning(const SourceLocation& loc, const std::string& msg, const std::string& hint) {
    diagnostics_.emplace_back(DiagnosticSeverity::Warning, loc, msg, hint);
}

bool GrassSema::hasErrors() const {
    for (const auto& d : diagnostics_) {
        if (d.severity == DiagnosticSeverity::Error) return true;
    }
    return false;
}

void GrassSema::printDiagnostics() const {
    for (const auto& d : diagnostics_) {
        std::cerr << d.format() << std::endl;
    }
}

std::string GrassSema::mangleName(const std::string& name) {
    std::string s = name;
    size_t p = 0;
    while ((p = s.find("::", p)) != std::string::npos) {
        s.replace(p, 2, "__");
        p += 2;
    }
    return s;
}

std::string GrassSema::currentNamespace() const {
    std::string ns;
    for (size_t i = 0; i < namespace_stack_.size(); ++i) {
        if (i > 0) ns += "::";
        ns += namespace_stack_[i];
    }
    return ns;
}

std::string GrassSema::qualifyName(const std::string& name) const {
    if (name.find("::") != std::string::npos) return name;
    std::string ns = currentNamespace();
    if (ns.empty()) return name;
    return ns + "::" + name;
}

const FunctionSymbol* GrassSema::findFunction(const std::string& name) const {
    // 1. Direct match
    auto it = functions_.find(name);
    if (it != functions_.end()) return &it->second;

    // 2. Mangled match
    std::string mangled = mangleName(name);
    it = functions_.find(mangled);
    if (it != functions_.end()) return &it->second;

    // 3. Namespace lookups
    if (name.find("::") == std::string::npos) {
        std::string cur_ns = currentNamespace();
        if (!cur_ns.empty()) {
            std::string q = cur_ns + "::" + name;
            it = functions_.find(q);
            if (it != functions_.end()) return &it->second;
            it = functions_.find(mangleName(q));
            if (it != functions_.end()) return &it->second;
        }

        auto us_it = using_symbols_.find(name);
        if (us_it != using_symbols_.end()) {
            it = functions_.find(us_it->second);
            if (it != functions_.end()) return &it->second;
            it = functions_.find(mangleName(us_it->second));
            if (it != functions_.end()) return &it->second;
        }

        for (const auto& ns : using_namespaces_) {
            std::string q = ns + "::" + name;
            it = functions_.find(q);
            if (it != functions_.end()) return &it->second;
            it = functions_.find(mangleName(q));
            if (it != functions_.end()) return &it->second;
        }
    } else {
        for (const auto& ns : using_namespaces_) {
            std::string q = ns + "::" + name;
            it = functions_.find(q);
            if (it != functions_.end()) return &it->second;
            it = functions_.find(mangleName(q));
            if (it != functions_.end()) return &it->second;
        }
    }
    return nullptr;
}

const StructSymbol* GrassSema::findStruct(const std::string& name) const {
    // 1. Direct match
    auto it = structs_.find(name);
    if (it != structs_.end()) return &it->second;

    // 2. Mangled match
    std::string mangled = mangleName(name);
    it = structs_.find(mangled);
    if (it != structs_.end()) return &it->second;

    // 3. Namespace lookups
    if (name.find("::") == std::string::npos) {
        std::string cur_ns = currentNamespace();
        if (!cur_ns.empty()) {
            std::string q = cur_ns + "::" + name;
            it = structs_.find(q);
            if (it != structs_.end()) return &it->second;
            it = structs_.find(mangleName(q));
            if (it != structs_.end()) return &it->second;
        }

        auto us_it = using_symbols_.find(name);
        if (us_it != using_symbols_.end()) {
            it = structs_.find(us_it->second);
            if (it != structs_.end()) return &it->second;
            it = structs_.find(mangleName(us_it->second));
            if (it != structs_.end()) return &it->second;
        }

        for (const auto& ns : using_namespaces_) {
            std::string q = ns + "::" + name;
            it = structs_.find(q);
            if (it != structs_.end()) return &it->second;
            it = structs_.find(mangleName(q));
            if (it != structs_.end()) return &it->second;
        }
    } else {
        for (const auto& ns : using_namespaces_) {
            std::string q = ns + "::" + name;
            it = structs_.find(q);
            if (it != structs_.end()) return &it->second;
            it = structs_.find(mangleName(q));
            if (it != structs_.end()) return &it->second;
        }
    }
    return nullptr;
}

SemaType GrassSema::resolveType(const std::string& type_name, int ptr_depth, const SourceLocation& loc) {
    SemaType base;
    if ((type_name.rfind("vec<", 0) == 0 || type_name.rfind("vector<", 0) == 0) &&
        type_name.size() >= 6 && type_name.back() == '>') {
        size_t lt = type_name.find('<');
        std::string inner = type_name.substr(lt + 1, type_name.size() - lt - 2);
        auto trim = [](std::string s) {
            size_t b = s.find_first_not_of(" \t\r\n");
            if (b == std::string::npos) return std::string();
            size_t e = s.find_last_not_of(" \t\r\n");
            return s.substr(b, e - b + 1);
        };
        inner = trim(inner);
        int inner_ptrs = 0;
        while (!inner.empty() && inner.back() == '*') {
            inner_ptrs++;
            inner.pop_back();
            inner = trim(inner);
        }
        SemaType elem_t = resolveType(inner, inner_ptrs, loc);
        base = SemaType::Vec(elem_t);
    } else if (type_name == "int" || type_name == "int32" || type_name == "int64" ||
        type_name == "i32" || type_name == "i64" ||
        type_name == "int32_t" || type_name == "int64_t" ||
        type_name == "uint32_t" || type_name == "uint64_t" ||
        type_name == "u32" || type_name == "u64" ||
        type_name == "u8" || type_name == "u16" || type_name == "i8" || type_name == "i16" ||
        type_name == "uint8_t" || type_name == "uint16_t" ||
        type_name == "int8_t" || type_name == "int16_t" ||
        type_name == "uint8" || type_name == "uint16" || type_name == "uint32" || type_name == "uint64" ||
        type_name == "int8" || type_name == "int16" ||
        type_name == "char" || type_name == "byte" || type_name == "size_t") {
        base = SemaType::Int();
    } else if (type_name == "float" || type_name == "double" || type_name == "f32" || type_name == "f64") {
        base = SemaType::Float();
    } else if (type_name == "bool") {
        base = SemaType::Bool();
    } else if (type_name == "string") {
        base = SemaType::String();
    } else if (type_name == "void") {
        base = SemaType::Void();
    } else if (type_name == "nullptr" || type_name == "null") {
        return SemaType::Nullptr();
    } else if (type_name == "var" || type_name == "auto" || type_name == "take" ||
               type_name == "set" || type_name == "let" || type_name == "fany" ||
               type_name == "invent" || type_name.empty()) {
        base = SemaType::Any();
    } else {
        const StructSymbol* st = findStruct(type_name);
        if (st) {
            base = SemaType::CustomStruct(st->name);
        } else {
            base = SemaType::CustomStruct(type_name);
        }
    }

    if (ptr_depth > 0) {
        SemaType ptr = base;
        for (int i = 0; i < ptr_depth; ++i) {
            ptr = SemaType::Ptr(ptr);
        }
        return ptr;
    }
    return base;
}

bool GrassSema::analyze(const ProgramAST& program) {
    diagnostics_.clear();
    scope_pool_.clear();
    scope_pool_.reserve(program.declarations.size() + 16);
    functions_.reserve(program.declarations.size() + 16);
    namespace_stack_.clear();
    using_namespaces_.clear();
    using_symbols_.clear();
    current_scope_ = nullptr;
    current_function_ = nullptr;
    current_struct_ = nullptr;

    if (program.is_strict) {
        strict_mode_ = true;
    }

    pass1_registerDeclarations(program);
    pass2_checkProgram(program);

    return !hasErrors();
}

void GrassSema::pass1_registerDeclarations(const ProgramAST& program) {
    auto register_decl = [&](auto& self, const DeclAST* decl) -> void {
        if (!decl) return;
        if (decl->node_kind == NodeKind::StructDecl) {
            auto st = static_cast<const StructDeclAST*>(decl);
            std::string qname = qualifyName(st->name);
            std::string mname = mangleName(qname);
            StructSymbol sym;
            sym.name = qname;
            sym.loc = st->loc;
            for (const auto& f : st->fields) {
                sym.fields[f.name] = resolveType(f.type_name, f.pointer_depth, f.loc);
                sym.field_order.push_back(f.name);
            }
            for (const auto& m : st->methods) {
                FunctionSymbol msym;
                msym.name = m->name;
                msym.return_type = m->is_destructor ? SemaType::Void() : resolveType(m->return_type, m->return_pointer_depth, m->loc);
                msym.loc = m->loc;
                msym.is_method = true;
                msym.is_destructor = m->is_destructor;
                msym.struct_name = qname;
                msym.has_body = (m->body != nullptr);
                for (const auto& p : m->params) {
                    msym.params.emplace_back(p.name, resolveType(p.type_name, p.pointer_depth, p.loc));
                }
                if (m->is_destructor) {
                    sym.has_explicit_destructor = true;
                    sym.has_destructor = true;
                }
                sym.methods[m->name] = msym;
            }
            structs_[qname] = sym;
            if (qname != mname) structs_[mname] = sym;
            if (st->name != qname && !structs_.count(st->name)) structs_[st->name] = sym;
        } else if (decl->node_kind == NodeKind::ClassDecl) {
            auto cls = static_cast<const ClassDeclAST*>(decl);
            std::string qname = qualifyName(cls->name);
            std::string mname = mangleName(qname);
            StructSymbol sym;
            sym.name = qname;
            sym.loc = cls->loc;
            sym.base_class = cls->base_class;
            for (const auto& f : cls->fields) {
                sym.fields[f.name] = resolveType(f.type_name, f.pointer_depth, f.loc);
                sym.field_order.push_back(f.name);
            }
            for (const auto& m : cls->methods) {
                FunctionSymbol msym;
                msym.name = m->name;
                msym.return_type = m->is_destructor ? SemaType::Void() : resolveType(m->return_type, m->return_pointer_depth, m->loc);
                msym.loc = m->loc;
                msym.is_method = true;
                msym.is_destructor = m->is_destructor;
                msym.struct_name = qname;
                msym.has_body = (m->body != nullptr);
                for (const auto& p : m->params) {
                    msym.params.emplace_back(p.name, resolveType(p.type_name, p.pointer_depth, p.loc));
                }
                if (m->is_destructor) {
                    sym.has_explicit_destructor = true;
                    sym.has_destructor = true;
                }
                sym.methods[m->name] = msym;
            }
            structs_[qname] = sym;
            if (qname != mname) structs_[mname] = sym;
            if (cls->name != qname && !structs_.count(cls->name)) structs_[cls->name] = sym;
        } else if (decl->node_kind == NodeKind::FunctionDecl) {
            auto fn = static_cast<const FunctionDeclAST*>(decl);
            std::string qname = qualifyName(fn->name);
            std::string mname = mangleName(qname);
            FunctionSymbol sym;
            sym.name = qname;
            sym.return_type = resolveType(fn->return_type, fn->return_pointer_depth, fn->loc);
            sym.loc = fn->loc;
            sym.has_body = (fn->body != nullptr);
            for (const auto& p : fn->params) {
                sym.params.emplace_back(p.name, resolveType(p.type_name, p.pointer_depth, p.loc));
            }
            functions_[qname] = sym;
            if (qname != mname) functions_[mname] = sym;
            if (fn->name != qname && !functions_.count(fn->name)) functions_[fn->name] = sym;
        } else if (decl->node_kind == NodeKind::NamespaceDecl) {
            auto ns = static_cast<const NamespaceDeclAST*>(decl);
            namespace_stack_.push_back(ns->name);
            for (const auto& d : ns->declarations) {
                self(self, d.get());
            }
            namespace_stack_.pop_back();
        } else if (decl->node_kind == NodeKind::UsingDecl) {
            auto u = static_cast<const UsingDeclAST*>(decl);
            if (u->is_namespace) {
                using_namespaces_.push_back(u->path);
            } else {
                size_t last_col = u->path.rfind("::");
                std::string sname = (last_col != std::string::npos) ? u->path.substr(last_col + 2) : u->path;
                using_symbols_[sname] = u->path;
            }
        } else if (decl->node_kind == NodeKind::ModuleDecl) {
            auto mod = static_cast<const ModuleDeclAST*>(decl);
            for (const auto& st : mod->structs) self(self, st.get());
            for (const auto& cls : mod->classes) self(self, cls.get());
            for (const auto& fn : mod->functions) self(self, fn.get());
        }
    };

    for (const auto& decl : program.declarations) {
        register_decl(register_decl, decl.get());
    }

    // Transitive has_destructor propagation across struct fields and base classes
    bool changed = true;
    while (changed) {
        changed = false;
        for (auto& kv : structs_) {
            if (kv.second.has_destructor) continue;
            bool need_dtor = false;
            if (!kv.second.base_class.empty()) {
                const StructSymbol* base = findStruct(kv.second.base_class);
                if (base && base->has_destructor) need_dtor = true;
            }
            if (!need_dtor) {
                for (const auto& fname : kv.second.field_order) {
                    auto fit = kv.second.fields.find(fname);
                    if (fit != kv.second.fields.end()) {
                        if (fit->second.is_vec()) {
                            need_dtor = true;
                            break;
                        }
                        if (fit->second.is_struct()) {
                            const StructSymbol* fstruct = findStruct(fit->second.name);
                            if (fstruct && fstruct->has_destructor) {
                                need_dtor = true;
                                break;
                            }
                        }
                    }
                }
            }
            if (need_dtor) {
                kv.second.has_destructor = true;
                changed = true;
            }
        }
    }
}


} // namespace grass::sema
