// Original regex implementation, test-only output oracle.
#pragma once
#include <regex>
std::string reference_rect_geometry(const char* vertex_source, bool quad) {
    const std::string vs = vertex_source ? vertex_source : "";
    const bool point = vs.find("gl_PointSize;") != std::string::npos;
    std::string gs = std::string("#version 430\nlayout(") + (quad ? "lines_adjacency" : "triangles") +
                     ") in;layout(triangle_strip,max_vertices=4) out;\nin gl_PerVertex{vec4 gl_Position;";
    if (point) gs += "float gl_PointSize;";
    gs += "}gl_in[];out gl_PerVertex{vec4 gl_Position;};\n";
    struct Varying {
        std::string name, input;
        bool flat;
    };
    std::vector<Varying> varyings;
    const std::regex output(
        R"(layout\s*\(location\s*=\s*(\d+)\)\s*((?:flat\s*|noperspective\s*)*)out\s+vec4\s+(\w+)\s*;)");
    for (std::sregex_iterator it(vs.begin(), vs.end(), output), end; it != end; ++it) {
        const auto& m = *it;
        const std::string input = "rbrew_rect_" + m[3].str();
        gs += "layout(location=" + m[1].str() + ") " + m[2].str() + "in vec4 " + input + "[];\n";
        gs += "layout(location=" + m[1].str() + ") " + m[2].str() + "out vec4 " + m[3].str() + ";\n";
        varyings.push_back({m[3].str(), input, m[2].str().find("flat") != std::string::npos});
    }
    gs += "void emitCorner(int a,int b,int c,bool generated){\n";
    for (const auto& v : varyings)
        gs += v.name + "=" +
              (v.flat ? v.input + (quad ? "[3]" : "[2]")
                      : ("generated?" + v.input + "[b]+" + v.input + "[c]-" + v.input + "[a]:" + v.input + "[a]")) +
              ";\n";
    gs += "gl_Position=generated?gl_in[b].gl_Position+gl_in[c].gl_Position-gl_in[a].gl_Position:gl_in[a].gl_Position;"
          "EmitVertex();}\n";
    if (quad)
        gs += "void "
              "main(){emitCorner(1,0,0,false);emitCorner(2,0,0,false);emitCorner(0,0,0,false);emitCorner(3,0,0,false);"
              "EndPrimitive();}\n";
    else
        gs += "void main(){float d01=length(gl_in[0].gl_Position.xy-gl_in[1].gl_Position.xy);float "
              "d02=length(gl_in[0].gl_Position.xy-gl_in[2].gl_Position.xy);float "
              "d12=length(gl_in[1].gl_Position.xy-gl_in[2].gl_Position.xy);int "
              "a=0,b=1,c=2;if(d01>d02&&d01>d12){a=2;b=1;c=0;}else "
              "if(d02>d01&&d02>d12){a=1;b=2;c=0;}emitCorner(a,b,c,false);emitCorner(b,a,c,false);emitCorner(c,a,b,"
              "false);emitCorner(a,b,c,true);EndPrimitive();}\n";
    return gs;
}
