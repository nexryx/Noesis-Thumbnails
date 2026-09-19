#include "renderer.hpp"
#include <iostream>

void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
template<class F>void rejects(F f,const char* message){bool threw=false;try{f();}catch(...){threw=true;}require(threw,message);}
int wmain(){
    try{
        require(nt::quote(L"a b\\")==L"\"a b\\\\\"","Trailing slash escaping");
        require(nt::quote(L"a\"b")==L"\"a\\\"b\"","Quote escaping");
        require(nt::wide(nt::utf8(L"model 日本.obj"))==L"model 日本.obj","Unicode round trip");
        require(nt::hash("abc")==L"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256");
        require(!nt::localPath(L"\\\\server\\model.obj"),"Network file must not reach provider metadata calls");
        auto dir=nt::fs::temp_directory_path()/(L"noesis-thumbnails-test-"+std::to_wstring(GetCurrentProcessId()));nt::fs::create_directories(dir);
        auto file=dir/L"cache.ntb";
        nt::Scene scene{};scene.kind=1;nt::Mesh mesh{};mesh.texture=-1;mesh.vertices={{{-1,-1,0},0,0},{{1,-1,0},1,0},{{0,1,0},.5f,1}};mesh.indices={0,1,2};scene.meshes.push_back(mesh);
        auto image=nt::render(scene,64);size_t visible=0;for(size_t i=3;i<image.pixels.size();i+=4)if(image.pixels[i])++visible;
        require(visible>100&&visible<4096,"Triangle rasterization and transparent background");
        nt::saveImage(file,image);auto loaded=nt::loadImage(file);require(loaded.pixels==image.pixels,"Cache round trip");
        auto small=nt::resize(loaded,16);require(small.width==16&&small.height==16,"Requested size limit");
        nt::Scene texture{};texture.kind=2;texture.textures.push_back({1,1,{200,100,50,128}});auto rgba=nt::render(texture);
        require(rgba.pixels[0]==25&&rgba.pixels[2]==100&&rgba.pixels[3]==128,"Premultiplied BGRA");
        auto glowing=scene;glowing.textures.push_back({1,1,{0,0,0,255}});glowing.meshes[0].texture=0;
        glowing.materialTextures.push_back({1,1,{0,255,255,0}}); // Emission alpha must not mask the model.
        auto& glow=glowing.meshes[0].material;glow.emissive=0;glow.emission[0]=glow.emission[1]=glow.emission[2]=1;
        auto emitted=nt::render(glowing,64);bool cyan=false;
        for(size_t p=0;p<emitted.pixels.size();p+=4)if(emitted.pixels[p]==255&&emitted.pixels[p+1]==255&&emitted.pixels[p+2]==0&&emitted.pixels[p+3]==255)cyan=true;
        require(cyan,"Emissive RGB must be visible independently of diffuse lighting and texture alpha");
        glow.emission[0]=glow.emission[1]=glow.emission[2]=0;auto dark=nt::render(glowing,64);
        for(size_t p=0;p<dark.pixels.size();p+=4)require(dark.pixels[p]==0&&dark.pixels[p+1]==0&&dark.pixels[p+2]==0,"Zero emission factor must remain dark");
        require(std::abs(nt::gamma(nt::linear(.5f))-.5f)<1e-5f,"sRGB transfer round trip");
        nt::Texture cube{1,1,{255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255,0,255,255,255,255,0,255,255},6};
        require(nt::environment(cube,{1,0,0}).x==1&&nt::environment(cube,{0,1,0}).z==1&&nt::environment(cube,{0,0,-1}).y==0,"Cubemap face selection");
        auto packed=scene;packed.textures.push_back({1,1,{255,0,0,255}});packed.meshes[0].texture=0;
        packed.materialTextures.push_back({1,1,{255,255,0,255}}); // glTF: AO=1, roughness=1, metal=0.
        packed.materialTextures.push_back({1,1,std::vector<uint8_t>(24,0),6});
        auto& pbr=packed.meshes[0].material;pbr.flags=(1u<<16)|(1u<<19);pbr.specular=0;pbr.environment=1;
        auto wrongMetal=nt::render(packed,64);
        pbr.specularSwizzle[0]=pbr.specularSwizzle[10]=0;pbr.specularSwizzle[2]=pbr.specularSwizzle[8]=1;
        auto dielectric=nt::render(packed,64);unsigned wrongRed=0,correctRed=0;
        for(size_t p=2;p<dielectric.pixels.size();p+=4){wrongRed=std::max(wrongRed,(unsigned)wrongMetal.pixels[p]);correctRed=std::max(correctRed,(unsigned)dielectric.pixels[p]);}
        require(wrongRed==0&&correctRed>100,"Packed AO must not be mistaken for metalness; apply the reader's channel swizzle");
        nt::atomicText(file,"broken");rejects([&]{nt::loadImage(file);},"Corrupt cache rejection");
        auto malformed=dir/L"scene.bin";uint32_t bad[]={0x3153544e,1,0,1,3,3,0xffffffff};nt::atomicWrite(malformed,bad,sizeof(bad));
        rejects([&]{nt::loadScene(malformed);},"Truncated scene rejection");
        auto writeMaterialScene=[&](const nt::Material& material,uint32_t normals){
            std::ofstream out(malformed,std::ios::binary|std::ios::trunc);
            uint32_t header[]={0x3153544e,1,0,1,3,3,0xffffffff};out.write((char*)header,sizeof(header));
            out.write((char*)mesh.vertices.data(),mesh.vertices.size()*sizeof(nt::Vertex));out.write((char*)mesh.indices.data(),mesh.indices.size()*4);
            uint32_t trailer[]={0x324d544e,0,1};out.write((char*)trailer,sizeof(trailer));out.write((char*)&material,sizeof(material));out.write((char*)&normals,4);
        };
        nt::Material valid;writeMaterialScene(valid,0);require(nt::loadScene(malformed).meshes.size()==1,"Material trailer round trip");
        auto invalid=valid;invalid.emissive=0;writeMaterialScene(invalid,0);rejects([&]{nt::loadScene(malformed);},"Out-of-range material texture rejection");
        invalid=valid;invalid.roughScale=NAN;writeMaterialScene(invalid,0);rejects([&]{nt::loadScene(malformed);},"Non-finite material factor rejection");
        writeMaterialScene(valid,2);rejects([&]{nt::loadScene(malformed);},"Mismatched normal count rejection");
        nt::atomicText(dir/L"model.obj","one");auto key1=nt::cacheKey(dir/L"model.obj",dir);nt::atomicText(dir/L"model.obj","changed");auto key2=nt::cacheKey(dir/L"model.obj",dir);require(key1!=key2,"Source change invalidates cache");
        nt::atomicText(dir/L"noesis-thumbnails.ini","new config");require(nt::cacheKey(dir/L"model.obj",dir)!=key2,"Configuration invalidates cache");
        // Only remove the exact files created by this test.
        for(auto name:{L"cache.ntb",L"scene.bin",L"model.obj",L"noesis-thumbnails.ini"})nt::remove(dir/name);RemoveDirectoryW(dir.c_str());
        std::cout<<"PASS: escaping, Unicode, hash, local paths, renderer, alpha, cache, malformed input, invalidation\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
}
