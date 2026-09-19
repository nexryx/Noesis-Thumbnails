#pragma once
#include "common.hpp"

namespace nt {
struct Vec { float x,y,z; Vec operator-(Vec b)const{return {x-b.x,y-b.y,z-b.z};} Vec operator+(Vec b)const{return {x+b.x,y+b.y,z+b.z};} Vec operator*(float f)const{return {x*f,y*f,z*f};} };
inline Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec normalize(Vec a){float n=std::sqrt(dot(a,a)); return n>1e-20f?Vec{a.x/n,a.y/n,a.z/n}:Vec{0,0,1};}
struct Vertex { Vec p; float u,v; };
static_assert(sizeof(Vertex)==20,"Bridge vertex layout must stay packed as five floats");
struct Material {
    int32_t normal=-1,specular=-1,emissive=-1,environment=-1;
    uint32_t flags=0,flags2=0;
    float diffuse[4]={1,1,1,1},emission[3]={0,0,0};
    float roughScale=1,roughBias=0,metalScale=1,metalBias=0;
    float specularSwizzle[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
};
static_assert(sizeof(Material)==132,"Material bridge layout");
struct Mesh { std::vector<Vertex> vertices; std::vector<uint32_t> indices; int32_t texture; Material material; std::vector<Vec> normals; };
struct Texture { uint32_t width,height; std::vector<uint8_t> rgba; uint32_t faces=1; };
struct Scene { uint32_t kind; std::vector<Texture> textures; std::vector<Mesh> meshes; std::vector<Texture> materialTextures; };
class Reader {
    std::ifstream file;
    uint64_t remaining;
public:
    explicit Reader(const fs::path& path):file(path,std::ios::binary),remaining(fs::file_size(path)){if(remaining>512ull<<20)throw std::runtime_error("Scene too large");}
    void bytes(void* out,size_t n){if(n>remaining)throw std::runtime_error("Truncated scene");file.read((char*)out,n);if(!file)throw std::runtime_error("Scene read failed");remaining-=n;}
    template<typename T>T get(){T x;bytes(&x,sizeof(x));return x;}
    bool done()const{return remaining==0;}
};
inline Scene loadScene(const fs::path& path) {
    Reader r(path); if(r.get<uint32_t>()!=0x3153544e)throw std::runtime_error("Invalid bridge protocol");
    Scene scene{};scene.kind=r.get<uint32_t>();auto textures=r.get<uint32_t>(),meshes=r.get<uint32_t>();
    if((scene.kind!=1 && scene.kind!=2)||textures>256||meshes>10000||(scene.kind==2&&(textures!=1||meshes)))throw std::runtime_error("Invalid scene counts");
    uint64_t texBytes=0,verts=0,indices=0;
    for(uint32_t t=0;t<textures;++t){Texture tex{};tex.width=r.get<uint32_t>();tex.height=r.get<uint32_t>();
        if(!tex.width||!tex.height||tex.width>1024||tex.height>1024)throw std::runtime_error("Invalid texture dimensions");
        size_t bytes=(size_t)tex.width*tex.height*4;texBytes+=bytes;if(texBytes>256ull<<20)throw std::runtime_error("Texture budget exceeded");
        tex.rgba.resize(bytes);r.bytes(tex.rgba.data(),bytes);scene.textures.push_back(std::move(tex));}
    for(uint32_t m=0;m<meshes;++m){auto nv=r.get<uint32_t>(),ni=r.get<uint32_t>();Mesh mesh{};mesh.texture=r.get<int32_t>();verts+=nv;indices+=ni;
        if(verts>2000000||indices>6000000||!nv||ni%3||mesh.texture < -1 || mesh.texture >= (int)textures)throw std::runtime_error("Invalid mesh counts");
        mesh.vertices.resize(nv);r.bytes(mesh.vertices.data(),nv*sizeof(Vertex));
        for(auto& v:mesh.vertices)if(!std::isfinite(v.p.x)||!std::isfinite(v.p.y)||!std::isfinite(v.p.z)||std::abs(v.p.x)>1e15f||std::abs(v.p.y)>1e15f||std::abs(v.p.z)>1e15f||!std::isfinite(v.u)||!std::isfinite(v.v))throw std::runtime_error("Non-finite geometry");
        mesh.indices.resize(ni);r.bytes(mesh.indices.data(),ni*4);for(auto i:mesh.indices)if(i>=nv)throw std::runtime_error("Invalid triangle index");scene.meshes.push_back(std::move(mesh));}
    if(!r.done()){
        if(scene.kind!=1||r.get<uint32_t>()!=0x324d544e)throw std::runtime_error("Invalid material trailer");
        auto count=r.get<uint32_t>(),meshCount=r.get<uint32_t>();
        if(count>256||meshCount!=meshes)throw std::runtime_error("Invalid material counts");
        for(uint32_t i=0;i<count;++i){Texture tex{};tex.width=r.get<uint32_t>();tex.height=r.get<uint32_t>();tex.faces=r.get<uint32_t>();
            if(!tex.width||!tex.height||tex.width>1024||tex.height>1024||(tex.faces!=1&&tex.faces!=6)||(tex.faces==6&&tex.width!=tex.height))throw std::runtime_error("Invalid material texture");
            size_t bytes=(size_t)tex.width*tex.height*tex.faces*4;texBytes+=bytes;if(texBytes>256ull<<20)throw std::runtime_error("Texture budget exceeded");
            tex.rgba.resize(bytes);r.bytes(tex.rgba.data(),bytes);scene.materialTextures.push_back(std::move(tex));}
        for(auto& mesh:scene.meshes){auto& m=mesh.material;r.bytes(&m,sizeof(m));
            for(auto index:{m.normal,m.specular,m.emissive,m.environment})if(index < -1||index >= (int)count)throw std::runtime_error("Invalid material texture index");
            for(float value:m.diffuse)if(!std::isfinite(value)||value<0||value>1000)throw std::runtime_error("Invalid material color");
            for(float value:m.emission)if(!std::isfinite(value)||value<0||value>1000)throw std::runtime_error("Invalid emission color");
            for(float value:{m.roughScale,m.roughBias,m.metalScale,m.metalBias})if(!std::isfinite(value)||std::abs(value)>1000)throw std::runtime_error("Invalid material factor");
            for(float value:m.specularSwizzle)if(!std::isfinite(value)||std::abs(value)>1000)throw std::runtime_error("Invalid material swizzle");
            auto normals=r.get<uint32_t>();if(normals&&normals!=mesh.vertices.size())throw std::runtime_error("Invalid normal count");
            mesh.normals.resize(normals);r.bytes(mesh.normals.data(),normals*sizeof(Vec));
            for(auto n:mesh.normals)if(!std::isfinite(n.x)||!std::isfinite(n.y)||!std::isfinite(n.z)||std::abs(n.x)>1e6f||std::abs(n.y)>1e6f||std::abs(n.z)>1e6f)throw std::runtime_error("Invalid vertex normal");}
    }
    if(!r.done())throw std::runtime_error("Trailing scene bytes");return scene;
}
inline float linear(float c){c=std::max(0.f,c);return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);}
inline float gamma(float c){c=std::max(0.f,c);if(c>=1)return 1;return c<=.0031308f?c*12.92f:1.055f*std::pow(c,1.f/2.4f)-.055f;}
inline Vec multiply(Vec a,Vec b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
inline Vec linear(Vec c){return {linear(c.x),linear(c.y),linear(c.z)};}
inline Vec sample(const Texture& t,float u,float v,unsigned face=0,bool clamp=false){
    if(clamp){u=std::clamp(u,0.f,1.f);v=std::clamp(v,0.f,1.f);}else{u-=std::floor(u);v-=std::floor(v);}
    auto x=std::min(t.width-1,(unsigned)(u*t.width)),y=std::min(t.height-1,(unsigned)(v*t.height));
    auto k=(((size_t)face*t.height+y)*t.width+x)*4;return {t.rgba[k]/255.f,t.rgba[k+1]/255.f,t.rgba[k+2]/255.f};
}
inline Vec environment(const Texture& t,Vec d){
    if(t.faces!=6)return sample(t,.5f+std::atan2(d.z,d.x)/(2*3.14159265f),.5f-std::asin(std::clamp(d.y,-1.f,1.f))/3.14159265f,0,true);
    float ax=std::abs(d.x),ay=std::abs(d.y),az=std::abs(d.z),u,v,a;unsigned face;
    if(ax>=ay&&ax>=az){a=ax;face=d.x>=0?0:1;u=d.x>=0?-d.z:d.z;v=-d.y;}
    else if(ay>=az){a=ay;face=d.y>=0?2:3;u=d.x;v=d.y>=0?d.z:-d.z;}
    else{a=az;face=d.z>=0?4:5;u=d.z>=0?d.x:-d.x;v=-d.y;}
    return sample(t,.5f+.5f*u/std::max(a,1e-20f),.5f+.5f*v/std::max(a,1e-20f),face,true);
}
inline void pixel(uint8_t* dst,float red,float green,float blue,float alpha,float light=1) {
    alpha=std::clamp(alpha,0.f,255.f);dst[3]=(uint8_t)alpha;
    dst[0]=(uint8_t)(std::clamp(blue*light,0.f,255.f)*alpha/255);
    dst[1]=(uint8_t)(std::clamp(green*light,0.f,255.f)*alpha/255);
    dst[2]=(uint8_t)(std::clamp(red*light,0.f,255.f)*alpha/255);
}
inline Image render(Scene scene,unsigned size=edge) {
    if(scene.kind==2){const auto& t=scene.textures.at(0);Image image{t.width,t.height,std::vector<uint8_t>(t.rgba.size())};
        for(size_t p=0;p<t.rgba.size();p+=4)pixel(image.pixels.data()+p,t.rgba[p],t.rgba[p+1],t.rgba[p+2],t.rgba[p+3]);return resize(image,size);}
    if(scene.meshes.empty())throw std::runtime_error("Empty scene");
    // Y-up, orthographic three-quarter view. No GPU/GL context in Explorer.
    Vec right=normalize({.8f,0,-.6f}),up=normalize({-.25f,.91f,-.33f}),forward=normalize(cross(right,up));
    for(auto& mesh:scene.meshes)for(auto& normal:mesh.normals){auto n=normal;normal=normalize({dot(n,right),dot(n,up),dot(n,forward)});}
    Vec lo{INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY};
    for(auto& m:scene.meshes)for(auto& v:m.vertices){auto p=v.p;v.p={dot(p,right),dot(p,up),dot(p,forward)};
        lo={std::min(lo.x,v.p.x),std::min(lo.y,v.p.y),std::min(lo.z,v.p.z)};hi={std::max(hi.x,v.p.x),std::max(hi.y,v.p.y),std::max(hi.z,v.p.z)};}
    float extent=std::max(hi.x-lo.x,hi.y-lo.y);if(!std::isfinite(extent)||extent<1e-15f)throw std::runtime_error("Degenerate model");
    // Render at 2x then filter down for smoother silhouettes.
    unsigned n=size*2;float scale=n*.84f/extent;
    for(auto& m:scene.meshes)for(auto& v:m.vertices){v.p.x=(v.p.x-(lo.x+hi.x)*.5f)*scale+n*.5f;v.p.y=n*.5f-(v.p.y-(lo.y+hi.y)*.5f)*scale;v.p.z=(v.p.z-lo.z)/extent;}
    Image result{n,n,std::vector<uint8_t>((size_t)n*n*4,0)};std::vector<float> zbuffer((size_t)n*n,-INFINITY);
    uint64_t work=0;const Vec light=normalize({-.3f,-.7f,1.f});
    for(auto& mesh:scene.meshes){const Texture* tex=mesh.texture>=0?&scene.textures[mesh.texture]:nullptr;
        const auto& material=mesh.material;bool pbr=(material.flags&((1u<<15)|(1u<<16)|(1u<<19)))!=0;
        auto channel=[&](int index)->const Texture*{return index>=0?&scene.materialTextures[index]:nullptr;};
        auto normalMap=channel(material.normal),specMap=channel(material.specular),emissiveMap=channel(material.emissive),envMap=channel(material.environment);
        for(size_t i=0;i<mesh.indices.size();i+=3){auto a=mesh.vertices[mesh.indices[i]],b=mesh.vertices[mesh.indices[i+1]],c=mesh.vertices[mesh.indices[i+2]];
            float area=(b.p.x-a.p.x)*(c.p.y-a.p.y)-(b.p.y-a.p.y)*(c.p.x-a.p.x);if(std::abs(area)<1e-8f)continue;
            int x0=std::clamp((int)std::floor(std::min({a.p.x,b.p.x,c.p.x})),0,(int)n-1),x1=std::clamp((int)std::ceil(std::max({a.p.x,b.p.x,c.p.x})),0,(int)n-1);
            int y0=std::clamp((int)std::floor(std::min({a.p.y,b.p.y,c.p.y})),0,(int)n-1),y1=std::clamp((int)std::ceil(std::max({a.p.y,b.p.y,c.p.y})),0,(int)n-1);
            work+=(uint64_t)(x1-x0+1)*(y1-y0+1);if(work>300000000)throw std::runtime_error("Raster work budget exceeded");
            Vec ab=b.p-a.p,ac=c.p-a.p;ab.x/=n*.84f;ab.y/=n*.84f;ac.x/=n*.84f;ac.y/=n*.84f;
            float shade=.38f+.62f*std::abs(dot(normalize(cross(ab,ac)),light));
            Vec e1{ab.x,-ab.y,ab.z},e2{ac.x,-ac.y,ac.z},faceNormal=normalize(cross(e1,e2));
            float du1=b.u-a.u,dv1=b.v-a.v,du2=c.u-a.u,dv2=c.v-a.v,det=du1*dv2-du2*dv1;
            Vec tangent{1,0,0},bitangent{0,1,0};
            if(std::abs(det)>1e-12f){tangent=(e1*dv2-e2*dv1)*(1/det);bitangent=(e2*du1-e1*du2)*(1/det);}
            for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){float px=x+.5f,py=y+.5f;
                float w1=((px-a.p.x)*(c.p.y-a.p.y)-(py-a.p.y)*(c.p.x-a.p.x))/area;
                float w2=((b.p.x-a.p.x)*(py-a.p.y)-(b.p.y-a.p.y)*(px-a.p.x))/area,w0=1-w1-w2;
                if(w0<0||w1<0||w2<0)continue;float z=w0*a.p.z+w1*b.p.z+w2*c.p.z;size_t p=(size_t)y*n+x;if(z<=zbuffer[p])continue;
                float red=180,green=196,blue=216,alpha=255;
                float u=w0*a.u+w1*b.u+w2*c.u,v=w0*a.v+w1*b.v+w2*c.v;
                if(tex){u-=std::floor(u);v-=std::floor(v);
                    unsigned tx=std::min(tex->width-1,(unsigned)(u*tex->width)),ty=std::min(tex->height-1,(unsigned)(v*tex->height));auto k=((size_t)ty*tex->width+tx)*4;
                    red=tex->rgba[k];green=tex->rgba[k+1];blue=tex->rgba[k+2];alpha=tex->rgba[k+3];if(alpha<128)continue;alpha=255;}
                if(pbr||emissiveMap){
                    Vec base=linear(Vec{red/255.f,green/255.f,blue/255.f});base=multiply(base,{material.diffuse[0],material.diffuse[1],material.diffuse[2]});
                    Vec color=base*shade;
                    if(pbr){
                        Vec normal=mesh.normals.empty()?faceNormal:normalize(mesh.normals[mesh.indices[i]]*w0+mesh.normals[mesh.indices[i+1]]*w1+mesh.normals[mesh.indices[i+2]]*w2);
                        if(normal.z<0)normal=normal*-1;
                        if(normalMap&&std::abs(det)>1e-12f){
                            Vec map=sample(*normalMap,u,v)*2-Vec{1,1,1};if(material.flags&(1u<<17))map.y=-map.y;
                            if(!(material.flags&(1u<<18)))map.z=std::sqrt(std::max(0.f,1-map.x*map.x-map.y*map.y));
                            Vec t=normalize(tangent-normal*dot(normal,tangent)),bvec=normalize(cross(normal,t));if(dot(bvec,bitangent)<0)bvec=bvec*-1;
                            normal=normalize(t*map.x+bvec*map.y+normal*map.z);
                        }
                        Vec raw=specMap?sample(*specMap,u,v):Vec{1,1,1};float values[4]={raw.x,raw.y,raw.z,1},swizzled[4]={};
                        if(specMap){auto tx=std::min(specMap->width-1,(unsigned)((u-std::floor(u))*specMap->width)),ty=std::min(specMap->height-1,(unsigned)((v-std::floor(v))*specMap->height));values[3]=specMap->rgba[((size_t)ty*specMap->width+tx)*4+3]/255.f;}
                        for(int row=0;row<4;++row)for(int col=0;col<4;++col)swizzled[row]+=material.specularSwizzle[row*4+col]*values[col];
                        Vec spec{swizzled[0],swizzled[1],swizzled[2]};float rough=1,metal=1;
                        if(material.flags&(1u<<19)){rough=spec.y;metal=spec.x;}
                        else if(specMap){rough=swizzled[3];metal=spec.y;}
                        if(material.flags2&(1u<<14))metal=spec.z;
                        rough=std::clamp(rough*material.roughScale+material.roughBias,.06f,1.f);metal=(material.flags&(1u<<16))?std::clamp(metal*material.metalScale+material.metalBias,0.f,1.f):0;
                        Vec f0=Vec{.04f,.04f,.04f}*(1-metal)+base*metal;
                        if((material.flags&(1u<<15))&&!(material.flags&(1u<<16)))f0=(material.flags2&(1u<<3))?linear(spec):spec;
                        float nv=std::max(0.f,normal.z),fresnel=std::pow(1-nv,5.f);Vec reflectance=f0+(Vec{1,1,1}-f0)*fresnel;
                        Vec reflection=normal*(2*nv)-Vec{0,0,1},world=normalize(right*reflection.x+up*reflection.y+forward*reflection.z);
                        if(material.flags&(1u<<20))world.z=-world.z;if(material.flags2&(1u<<7))world.y=-world.y;
                        Vec env=envMap?linear(environment(*envMap,world)):Vec{.6f,.6f,.6f};
                        auto lightDir=normalize(Vec{-.35f,.65f,1}),halfway=normalize(lightDir+Vec{0,0,1});
                        float nl=std::max(0.f,dot(normal,lightDir)),exponent=std::max(2.f,2/(rough*rough)-2);
                        float highlight=std::pow(std::max(0.f,dot(normal,halfway)),exponent)*(1-rough)*1.8f;
                        color=base*((.22f+.9f*nl)*(1-metal))+multiply(env,reflectance)*(.7f+.3f*(1-rough))+reflectance*highlight;
                    }
                    if(emissiveMap)color=color+multiply(linear(sample(*emissiveMap,u,v)),{material.emission[0],material.emission[1],material.emission[2]});
                    red=gamma(color.x)*255;green=gamma(color.y)*255;blue=gamma(color.z)*255;
                    zbuffer[p]=z;pixel(result.pixels.data()+p*4,red,green,blue,alpha);
                }else{zbuffer[p]=z;pixel(result.pixels.data()+p*4,red,green,blue,alpha,shade);}
            }
        }
    } return resize(result,size);
}
} // namespace nt
