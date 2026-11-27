// Standalone diagnostic, not renderer code. cl /EHsc attachment.cpp d3d11.lib d3dcompiler.lib
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <array>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <cstring>
using Microsoft::WRL::ComPtr;
void check(HRESULT hr) { if(FAILED(hr)) { std::cerr<<"HRESULT "<<std::hex<<hr<<'\n'; throw std::runtime_error("D3D failure"); } }
int main(int argc,char** argv) try {
    if(argc!=2) return 2;
    std::array<std::array<float,4>,16> values{};
    std::ifstream in(argv[1]); unsigned count=0;
    while(count<values.size() && in>>values[count][0]>>values[count][1]>>values[count][2]>>values[count][3]) ++count;
    if(!count || !in.eof()) throw std::runtime_error("Expected 1..16 float4 rows");
    ComPtr<ID3D11Device> device; ComPtr<ID3D11DeviceContext> context; D3D_FEATURE_LEVEL level;
    check(D3D11CreateDevice(nullptr,D3D_DRIVER_TYPE_HARDWARE,nullptr,0,nullptr,0,D3D11_SDK_VERSION,&device,&level,&context));
    ComPtr<IDXGIDevice> dxgi; check(device.As(&dxgi)); ComPtr<IDXGIAdapter> adapter; check(dxgi->GetAdapter(&adapter));
    DXGI_ADAPTER_DESC desc{}; check(adapter->GetDesc(&desc)); LARGE_INTEGER version{};
    HRESULT versionStatus=adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice),&version);
    std::cout<<std::setprecision(9)<<"{\"vendor\":"<<desc.VendorId<<",\"device\":"<<desc.DeviceId<<",\"driverVersion\":"<<version.QuadPart<<",\"versionStatus\":"<<versionStatus<<",\"featureLevel\":"<<level<<",\"states\":[";
    const char* shader=R"(
cbuffer Constants : register(b0) { float4 values[16]; };
float4 vs(uint id:SV_VertexID):SV_Position {
    return float4(id==1 ? 3 : -1, id==2 ? -3 : 1, 0, 1);
}
struct Output { float4 bytes:SV_Target0; float4 exact:SV_Target1; };
Output ps(float4 pos:SV_Position) { Output o; o.bytes=values[(uint)pos.x]; o.exact=o.bytes; return o; }
)";
    auto compile=[&](const char* entry,const char* profile) {
        ComPtr<ID3DBlob> code,errors;
        HRESULT hr=D3DCompile(shader,strlen(shader),nullptr,nullptr,nullptr,entry,profile,D3DCOMPILE_ENABLE_STRICTNESS,0,&code,&errors);
        if(errors) std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),errors->GetBufferSize());
        check(hr); return code;
    };
    auto vc=compile("vs","vs_5_0"),pc=compile("ps","ps_5_0");
    ComPtr<ID3D11VertexShader> vs; ComPtr<ID3D11PixelShader> ps;
    check(device->CreateVertexShader(vc->GetBufferPointer(),vc->GetBufferSize(),nullptr,&vs));
    check(device->CreatePixelShader(pc->GetBufferPointer(),pc->GetBufferSize(),nullptr,&ps));
    D3D11_BUFFER_DESC bd{}; bd.ByteWidth=sizeof(values);bd.Usage=D3D11_USAGE_IMMUTABLE;bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA data{};data.pSysMem=values.data(); ComPtr<ID3D11Buffer> buffer;check(device->CreateBuffer(&bd,&data,&buffer));
    context->PSSetConstantBuffers(0,1,buffer.GetAddressOf());context->VSSetShader(vs.Get(),nullptr,0);context->PSSetShader(ps.Get(),nullptr,0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    D3D11_RASTERIZER_DESC rd{};rd.FillMode=D3D11_FILL_SOLID;rd.CullMode=D3D11_CULL_NONE;rd.DepthClipEnable=TRUE;
    ComPtr<ID3D11RasterizerState> raster;check(device->CreateRasterizerState(&rd,&raster));context->RSSetState(raster.Get());
    D3D11_VIEWPORT viewport{0,0,float(count),1,0,1};context->RSSetViewports(1,&viewport);
    std::array<ComPtr<ID3D11Texture2D>,2> textures,staging;std::array<ComPtr<ID3D11RenderTargetView>,2> views;
    for(unsigned t=0;t<2;++t) {
        D3D11_TEXTURE2D_DESC td{};td.Width=count;td.Height=td.MipLevels=td.ArraySize=td.SampleDesc.Count=1;
        td.Format=t ? DXGI_FORMAT_R32G32B32A32_FLOAT : DXGI_FORMAT_R8G8B8A8_UNORM;td.BindFlags=D3D11_BIND_RENDER_TARGET;
        check(device->CreateTexture2D(&td,nullptr,&textures[t]));check(device->CreateRenderTargetView(textures[t].Get(),nullptr,&views[t]));
        td.BindFlags=0;td.Usage=D3D11_USAGE_STAGING;td.CPUAccessFlags=D3D11_CPU_ACCESS_READ;check(device->CreateTexture2D(&td,nullptr,&staging[t]));
    }
    ID3D11RenderTargetView* targets[]={views[0].Get(),views[1].Get()};
    const char* names[]={"disabled","one-zero","one-inv-src-alpha"};
    for(unsigned mode=0;mode<3;++mode) {
        D3D11_BLEND_DESC blend{};blend.IndependentBlendEnable=TRUE;
        for(auto& target:blend.RenderTarget) {
            target.RenderTargetWriteMask=D3D11_COLOR_WRITE_ENABLE_ALL;
            target.SrcBlend=target.SrcBlendAlpha=D3D11_BLEND_ONE;target.DestBlend=target.DestBlendAlpha=D3D11_BLEND_ZERO;
            target.BlendOp=target.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        }
        auto& b=blend.RenderTarget[0];b.BlendEnable=mode!=0;b.SrcBlend=b.SrcBlendAlpha=D3D11_BLEND_ONE;
        b.DestBlend=b.DestBlendAlpha=mode==2 ? D3D11_BLEND_INV_SRC_ALPHA : D3D11_BLEND_ZERO;b.BlendOp=b.BlendOpAlpha=D3D11_BLEND_OP_ADD;
        ComPtr<ID3D11BlendState> state;check(device->CreateBlendState(&blend,&state));context->OMSetBlendState(state.Get(),nullptr,0xffffffff);
        const float clear[4]={};for(auto& v:views) context->ClearRenderTargetView(v.Get(),clear);
        context->OMSetRenderTargets(2,targets,nullptr);context->Draw(3,0);context->OMSetRenderTargets(0,nullptr,nullptr);
        std::cout<<(mode ? "," : "")<<"{\"name\":\""<<names[mode]<<"\",\"rgba8\":[";
        for(unsigned t=0;t<2;++t) {
            context->CopyResource(staging[t].Get(),textures[t].Get());D3D11_MAPPED_SUBRESOURCE mapped{};
            check(context->Map(staging[t].Get(),0,D3D11_MAP_READ,0,&mapped));
            if(t) std::cout<<"],\"rgba32f\":[";
            for(unsigned i=0;i<count;++i) {
                std::cout<<(i ? ",[" : "[");
                for(unsigned k=0;k<4;++k) {
                    if(k) std::cout<<',';
                    if(t) {float v=static_cast<const float*>(mapped.pData)[4*i+k];if(v!=values[i][k]) throw std::runtime_error("Float companion mismatch");std::cout<<v;}
                    else std::cout<<unsigned(static_cast<const unsigned char*>(mapped.pData)[4*i+k]);
                }
                std::cout<<']';
            }
            context->Unmap(staging[t].Get(),0);
        }
        std::cout<<"]}";
    }
    std::cout<<"]}\n";return 0;
} catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
