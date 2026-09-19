// Private material trailer for NTS1 scenes. Texture indices come from Noesis,
// not filenames: embedded glTF images can all be named "pngout.png".
#include <algorithm>
#include <array>
#include <cstdint>
#include <stdexcept>

namespace ntmaterials {
struct Material {
    int32_t normal=-1,specular=-1,emissive=-1,environment=-1;
    uint32_t flags=0,flags2=0;
    float diffuse[4]={1,1,1,1},emission[3]={0,0,0};
    float roughScale=1,roughBias=0,metalScale=1,metalBias=0;
    float specularSwizzle[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
};
static_assert(sizeof(Material)==132,"Material bridge layout");
struct Pixels { uint32_t width,height,faces; std::vector<uint8_t> bytes; };
struct File {
    FILE* value;
    explicit File(const wchar_t* path):value(_wfopen(path,L"ab")){if(!value)throw std::runtime_error("Material output unavailable");}
    ~File(){if(value)fclose(value);}
    void write(const void* data,size_t size){if(size&&fwrite(data,1,size,value)!=size)throw std::runtime_error("Material write failed");}
    template<class T>void put(const T& value){write(&value,sizeof(value));}
};
struct Shared {
    noeRAPI_t* rapi;sharedModel_t* model;
    ~Shared(){if(model)rapi->Noesis_FreeSharedModel(model);}
};
struct Decoded {
    noeRAPI_t* rapi;BYTE* pixels;bool owned;
    ~Decoded(){if(owned&&pixels)rapi->Noesis_UnpooledFree(pixels);}
};
}

extern "C" __declspec(dllexport) int NT_AppendMaterials(int module,const wchar_t* path,const int* selections,int count,const float* rotation){
    using namespace ntmaterials;
    if(!g_nfn||!path||!selections||!rotation||count<1||count>10000)return 0;
    try{
        auto rapi=g_nfn->NPAPI_GetModuleRAPI(module);if(!rapi)return 0;
        auto loaded=rapi->Noesis_GetLoadedModel(0);if(!loaded)return 0;
        Shared shared{rapi,rapi->rpgGetSharedModel(loaded,NMSHAREDFL_LOCALPOOL)};if(!shared.model)return 0;
        auto data=shared.model->matData;
        std::vector<Material> materials;std::vector<Pixels> textures;std::vector<int> sourceIndices;
        uint64_t totalBytes=0;
        auto texture=[&](int index)->int {
            if(!data||index<0||index>=data->numTextures)return -1;
            auto found=std::find(sourceIndices.begin(),sourceIndices.end(),index);
            if(found!=sourceIndices.end())return (int)(found-sourceIndices.begin());
            auto& src=data->textures[index];if(!src.data||src.w<1||src.h<1)return -1;
            if(src.w>32768||src.h>32768||textures.size()>=256)throw std::runtime_error("Material texture budget");
            uint32_t faces=(src.flags&NTEXFLAG_CUBEMAP)?6:1;
            float scale=std::min(1.f,1024.f/std::max(src.w,src.h));
            Pixels dst{(uint32_t)std::max(1,(int)(src.w*scale)),(uint32_t)std::max(1,(int)(src.h*scale)),faces,{}};
            size_t faceBytes=(size_t)dst.width*dst.height*4;
            totalBytes+=faceBytes*faces;if(totalBytes>256ull*1024*1024)throw std::runtime_error("Material texture budget");
            dst.bytes.resize(faceBytes*faces);
            TNoeSize stride=0;
            for(int mip=0;mip<std::max(1,src.mipCount);++mip)stride+=rapi->Image_GetMipSize(&src,mip,false);
            if(stride<=0||stride>src.dataLen/faces)throw std::runtime_error("Invalid texture face layout");
            for(uint32_t face=0;face<faces;++face){
                bool owned=false;auto pixels=rapi->Image_GetTexRGBAOffset(&src,owned,stride*face);
                Decoded decoded{rapi,pixels,owned};if(!pixels)throw std::runtime_error("Material texture decode failed");
                rapi->Noesis_ResampleImageBilinear(pixels,src.w,src.h,dst.bytes.data()+faceBytes*face,dst.width,dst.height);
            }
            sourceIndices.push_back(index);textures.push_back(std::move(dst));return (int)textures.size()-1;
        };
        for(int i=0;i<count;++i){
            int index=selections[i*2],vertices=selections[i*2+1];
            if(index<0||index>=shared.model->numMeshes||shared.model->meshes[index].numVerts!=vertices)throw std::runtime_error("Mesh mapping mismatch");
            auto& mesh=shared.model->meshes[index];Material out;
            if(data&&mesh.materialIdx>=0&&mesh.materialIdx<data->numMaterials){
                auto& mat=data->materials[mesh.materialIdx];
                out.flags=mat.flags;out.flags2=mat.ex?mat.ex->flags2:0;
                std::copy(mat.diffuse,mat.diffuse+4,out.diffuse);
                // Keep legacy untextured fallback shading; material color is
                // multiplied with an existing base texture in this revision.
                if(mat.flags&NMATFLAG_PBR_ANY){
                    out.normal=texture(mat.normalTexIdx);out.specular=texture(mat.specularTexIdx);out.environment=texture(mat.envTexIdx);
                    if(mat.ex){out.roughScale=mat.ex->roughnessScale;out.roughBias=mat.ex->roughnessBias;out.metalScale=mat.ex->metalScale;out.metalBias=mat.ex->metalBias;}
                    if(mat.ex&&mat.ex->pSpecSwizzle)std::copy(mat.ex->pSpecSwizzle,mat.ex->pSpecSwizzle+16,out.specularSwizzle);
                }
                if(mat.nextPass&&mat.nextPass->blendSrc==NOEBLEND_ONE&&mat.nextPass->blendDst==NOEBLEND_ONE){
                    out.emissive=texture(mat.nextPass->texIdx);
                    std::copy(mat.nextPass->diffuse,mat.nextPass->diffuse+3,out.emission);
                }
            }
            materials.push_back(out);
        }
        File file(path);file.put(uint32_t(0x324d544e));file.put(uint32_t(textures.size()));file.put(uint32_t(count));
        for(auto& tex:textures){file.put(tex.width);file.put(tex.height);file.put(tex.faces);file.write(tex.bytes.data(),tex.bytes.size());}
        for(int i=0;i<count;++i){
            file.put(materials[i]);auto& mesh=shared.model->meshes[selections[i*2]];
            uint32_t normals=mesh.normals?mesh.numVerts:0;file.put(normals);
            for(uint32_t v=0;v<normals;++v){
                const auto& normal=mesh.normals[v];float n[]={normal.x,normal.y,normal.z},rotated[3];
                for(int row=0;row<3;++row)rotated[row]=n[0]*rotation[row*3]+n[1]*rotation[row*3+1]+n[2]*rotation[row*3+2];
                file.write(rotated,sizeof(rotated));
            }
        }
        return fflush(file.value)==0?1:0;
    }catch(...){return 0;}
}
