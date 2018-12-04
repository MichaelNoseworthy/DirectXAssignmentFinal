//***************************************************************************************
// CameraAndDynamicIndexingApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "Common/d3dApp.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"
#include "Common/GeometryGenerator.h"
#include "Common/Camera.h"
#include "FrameResource.h"
#include "Common/Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;
float rotationlimit = 0;


// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;
    RenderItem(const RenderItem& rhs) = delete;
 
    // World matrix of the shape that describes the object's local space
    // relative to the world space, which defines the position, orientation,
    // and scale of the object in the world.
    XMFLOAT4X4 World = MathHelper::Identity4x4();

	XMFLOAT4X4 TexTransform = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;

	Material* Mat = nullptr;
	MeshGeometry* Geo = nullptr;

    // Primitive topology.
    D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    // DrawIndexedInstanced parameters.
    UINT IndexCount = 0;
    UINT StartIndexLocation = 0;
    int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTested,
	Count
};

class CameraAndDynamicIndexingApp : public D3DApp
{
public:
    CameraAndDynamicIndexingApp(HINSTANCE hInstance);
    CameraAndDynamicIndexingApp(const CameraAndDynamicIndexingApp& rhs) = delete;
    CameraAndDynamicIndexingApp& operator=(const CameraAndDynamicIndexingApp& rhs) = delete;
    ~CameraAndDynamicIndexingApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialBuffer(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	//wave
	void UpdateWaves(const GameTimer& gt);

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayout();
    void BuildShapeGeometry();
	void BuildSkullGeometry();
	//waves
	void BuildLandGeometry();
	void BuildWavesGeometry();

    void BuildPSOs();
    void BuildFrameResources();
    void BuildMaterials();
    void BuildRenderItems();
    void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);

	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

	float GetHillsHeight(float x, float z)const;
	XMFLOAT3 GetHillsNormal(float x, float z)const;
private:

    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource* mCurrFrameResource = nullptr;
    int mCurrFrameResourceIndex = 0;

    UINT mCbvSrvDescriptorSize = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
 
	RenderItem* mWavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;

	std::unique_ptr<Waves> mWaves;

    PassConstants mMainPassCB;

	Camera mCamera;

    POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
    PSTR cmdLine, int showCmd)
{
    // Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

    try
    {
        CameraAndDynamicIndexingApp theApp(hInstance);
        if(!theApp.Initialize())
            return 0;

        return theApp.Run();
    }
    catch(DxException& e)
    {
        MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
        return 0;
    }
}

CameraAndDynamicIndexingApp::CameraAndDynamicIndexingApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

CameraAndDynamicIndexingApp::~CameraAndDynamicIndexingApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool CameraAndDynamicIndexingApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific, 
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

	mCamera.SetPosition(0.0f, 2.0f, -15.0f);
 
	LoadTextures();
    BuildRootSignature();
	BuildDescriptorHeaps();
    BuildShadersAndInputLayout();
    BuildShapeGeometry();
	BuildLandGeometry();
	BuildWavesGeometry();
	BuildSkullGeometry();
	BuildMaterials();
    BuildRenderItems();
    BuildFrameResources();
    BuildPSOs();

    // Execute the initialization commands.
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Wait until initialization is complete.
    FlushCommandQueue();

    return true;
}
 
void CameraAndDynamicIndexingApp::OnResize()
{
    D3DApp::OnResize();

	mCamera.SetLens(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void CameraAndDynamicIndexingApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);

    // Cycle through the circular frame resource array.
    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    // Has the GPU finished processing the commands of the current frame resource?
    // If not, wait until the GPU has completed commands up to this fence point.
    if(mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
    {
        HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }

	AnimateMaterials(gt);
	UpdateObjectCBs(gt);
	UpdateMaterialBuffer(gt);
	UpdateMainPassCB(gt);
	UpdateWaves(gt);
}

void CameraAndDynamicIndexingApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

    // Reuse the memory associated with command recording.
    // We can only reset when the associated command lists have finished execution on the GPU.
    ThrowIfFailed(cmdListAlloc->Reset());

    // A command list can be reset after it has been added to the command queue via ExecuteCommandList.
    // Reusing the command list reuses memory.
    ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(1, passCB->GetGPUVirtualAddress());

	// Bind all the materials used in this scene.  For structured buffers, we can bypass the heap and 
	// set as a root descriptor.
	auto matBuffer = mCurrFrameResource->MaterialBuffer->Resource();
	mCommandList->SetGraphicsRootShaderResourceView(2, matBuffer->GetGPUVirtualAddress());

	// Bind all the textures used in this scene.  Observe
    // that we only have to specify the first descriptor in the table.  
    // The root signature knows how many descriptors are expected in the table.
	mCommandList->SetGraphicsRootDescriptorTable(3, mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());

    DrawRenderItems(mCommandList.Get(), mOpaqueRitems);

    // Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

    // Done recording commands.
    ThrowIfFailed(mCommandList->Close());

    // Add the command list to the queue for execution.
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // Swap the back and front buffers
    ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    // Advance the fence value to mark commands up to this fence point.
    mCurrFrameResource->Fence = ++mCurrentFence;

    // Add an instruction to the command queue to set a new fence point. 
    // Because we are on the GPU timeline, the new fence point won't be 
    // set until the GPU finishes processing all the commands prior to this Signal().
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void CameraAndDynamicIndexingApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void CameraAndDynamicIndexingApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void CameraAndDynamicIndexingApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
		// Make each pixel correspond to a quarter of a degree.
		float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}
 
void CameraAndDynamicIndexingApp::OnKeyboardInput(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();

	if(GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(10.0f*dt);

	if(GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-10.0f*dt);

	if(GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-10.0f*dt);

	if(GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(10.0f*dt);

	if (rotationlimit >= -0.90)
	{
		if (GetAsyncKeyState('Q') & 0x8000)
		{
			rotationlimit -= 1 * dt;
			mCamera.Roll(1.0f*dt);
		}
	}

	if (rotationlimit <= 0.90)
	{
		if (GetAsyncKeyState('E') & 0x8000)
		{
			rotationlimit += 1 * dt;
			mCamera.Roll(-1.0f*dt);
		}
	}

	mCamera.UpdateViewMatrix();
}
 
void CameraAndDynamicIndexingApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if (tu >= 1.0f)
		tu -= 1.0f;

	if (tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void CameraAndDynamicIndexingApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for(auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if(e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));
			objConstants.MaterialIndex = e->Mat->MatCBIndex;

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void CameraAndDynamicIndexingApp::UpdateMaterialBuffer(const GameTimer& gt)
{
	auto currMaterialBuffer = mCurrFrameResource->MaterialBuffer.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));
			matData.DiffuseMapIndex = mat->DiffuseSrvHeapIndex;

			currMaterialBuffer->CopyData(mat->MatCBIndex, matData);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void CameraAndDynamicIndexingApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.EyePosW = mCamera.GetPosition3f();
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();
	mMainPassCB.AmbientLight = { 0.25f, 0.25f, 0.35f, 1.0f };
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.8f, 0.8f, 0.8f };
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 0.2f, 0.2f, 0.2f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void CameraAndDynamicIndexingApp::BuildLandGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);

	//
	// Extract the vertex elements we are interested and apply the height function to
	// each vertex.  In addition, color the vertices based on their height so we have
	// sandy looking beaches, grassy low hills, and snow mountain peaks.
	//

	std::vector<Vertex> vertices(grid.Vertices.size());
	for (size_t i = 0; i < grid.Vertices.size(); ++i)
	{
		auto& p = grid.Vertices[i].Position;
		vertices[i].Pos = p;
		vertices[i].Pos.y = GetHillsHeight(p.x, p.z);
		vertices[i].Normal = GetHillsNormal(p.x, p.z);
		vertices[i].TexC = grid.Vertices[i].TexC;
	}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	std::vector<std::uint16_t> indices = grid.GetIndices16();
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "landGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["landGeo"] = std::move(geo);
}

void CameraAndDynamicIndexingApp::BuildWavesGeometry()
{
	std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

	// Iterate over each quad.
	int m = mWaves->RowCount();
	int n = mWaves->ColumnCount();
	int k = 0;
	for (int i = 0; i < m - 1; ++i)
	{
		for (int j = 0; j < n - 1; ++j)
		{
			indices[k] = i * n + j;
			indices[k + 1] = i * n + j + 1;
			indices[k + 2] = (i + 1)*n + j;

			indices[k + 3] = (i + 1)*n + j;
			indices[k + 4] = i * n + j + 1;
			indices[k + 5] = (i + 1)*n + j + 1;

			k += 6; // next quad
		}
	}

	UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "waterGeo";

	// Set dynamically.
	geo->VertexBufferCPU = nullptr;
	geo->VertexBufferGPU = nullptr;

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["grid"] = submesh;

	mGeometries["waterGeo"] = std::move(geo);
}

void CameraAndDynamicIndexingApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if ((mTimer.TotalTime() - t_base) >= 0.25f)
	{
		t_base += 0.25f;

		int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
		int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

		float r = MathHelper::RandF(0.2f, 0.5f);

		mWaves->Disturb(i, j, r);
	}

	// Update the wave simulation.
	mWaves->Update(gt.DeltaTime());

	// Update the wave vertex buffer with the new solution.
	auto currWavesVB = mCurrFrameResource->WavesVB.get();
	for (int i = 0; i < mWaves->VertexCount(); ++i)
	{
		Vertex v;

		v.Pos = mWaves->Position(i);
		v.Normal = mWaves->Normal(i);

		// Derive tex-coords from position by 
		// mapping [-w/2,w/2] --> [0,1]
		v.TexC.x = 0.5f + v.Pos.x / mWaves->Width();
		v.TexC.y = 0.5f - v.Pos.z / mWaves->Depth();

		currWavesVB->CopyData(i, v);
	}

	// Set the dynamic VB of the wave renderitem to the current frame VB.
	mWavesRitem->Geo->VertexBufferGPU = currWavesVB->Resource();
}

void CameraAndDynamicIndexingApp::LoadTextures()
{
	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex";
	bricksTex->Filename = L"Textures/bricks.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), bricksTex->Filename.c_str(),
		bricksTex->Resource, bricksTex->UploadHeap));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"Textures/stone.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	auto tileTex = std::make_unique<Texture>();
	tileTex->Name = "tileTex";
	tileTex->Filename = L"Textures/tile.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), tileTex->Filename.c_str(),
		tileTex->Resource, tileTex->UploadHeap));

	auto crateTex = std::make_unique<Texture>();
	crateTex->Name = "crateTex";
	crateTex->Filename = L"Textures/WoodCrate01.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), crateTex->Filename.c_str(),
		crateTex->Resource, crateTex->UploadHeap));

	auto grassTex = std::make_unique<Texture>();
	grassTex->Name = "grassTex";
	grassTex->Filename = L"Textures/grass.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), grassTex->Filename.c_str(),
		grassTex->Resource, grassTex->UploadHeap));

	auto waterTex = std::make_unique<Texture>();
	waterTex->Name = "waterTex";
	waterTex->Filename = L"Textures/water1.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), waterTex->Filename.c_str(),
		waterTex->Resource, waterTex->UploadHeap));

	mTextures[bricksTex->Name] = std::move(bricksTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
	mTextures[tileTex->Name] = std::move(tileTex);
	mTextures[crateTex->Name] = std::move(crateTex);
	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[waterTex->Name] = std::move(waterTex);
}

void CameraAndDynamicIndexingApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);
    slotRootParameter[2].InitAsShaderResourceView(0, 1);
	slotRootParameter[3].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);


	auto staticSamplers = GetStaticSamplers();

    // A root signature is an array of root parameters.
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(4, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // create a root signature with a single slot which points to a descriptor range consisting of a single constant buffer
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorBlob = nullptr;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

    if(errorBlob != nullptr)
    {
        ::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

void CameraAndDynamicIndexingApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 4;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto bricksTex = mTextures["bricksTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto tileTex = mTextures["tileTex"]->Resource;
	auto crateTex = mTextures["crateTex"]->Resource;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	//srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.Format = bricksTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = bricksTex->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = stoneTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = stoneTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = tileTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tileTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);

	srvDesc.Format = crateTex->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = crateTex->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(crateTex.Get(), &srvDesc, hDescriptor);
}

void CameraAndDynamicIndexingApp::BuildShadersAndInputLayout()
{
	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "PS", "ps_5_1");
	
    mInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
}

void CameraAndDynamicIndexingApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(100.0f, 100.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(1, 1, 1, 3); /////////////////////////  ///1
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1); //pyramid
	GeometryGenerator::MeshData rhombo = geoGen.CreateRhombo(1); //rhombo
	GeometryGenerator::MeshData prism = geoGen.CreatePrism(1); //prism
	GeometryGenerator::MeshData hexagon = geoGen.CreateHexagon(1); //hexagon
	GeometryGenerator::MeshData triangleEq = geoGen.CreateTriangleEq(1); //TriangleEq
	GeometryGenerator::MeshData triangleRectSqr = geoGen.CreateTriangleRectSqr(1); //triangleRectSqr

	//
	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.
	//

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();
	UINT diamondVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();///////////////// //2
	UINT pyramidVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size(); //pyramid
	UINT rhomboVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size(); //rhombo
	UINT prismVertexOffset = rhomboVertexOffset + (UINT)rhombo.Vertices.size();
	UINT hexagonVertexOffset = prismVertexOffset + (UINT)prism.Vertices.size();
	UINT triangleEqVertexOffset = hexagonVertexOffset + (UINT)hexagon.Vertices.size();//TriangleEq
	UINT triangleRectSqrVertexOffset = triangleEqVertexOffset + (UINT)triangleEq.Vertices.size();	//triangleRectSqr

	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();
	UINT diamondIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();/////////////////// //3
	UINT pyramidIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();//pyramind
	UINT rhomboIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();//rhombo
	UINT prismIndexOffset = rhomboIndexOffset + (UINT)rhombo.Indices32.size();//prism
	UINT hexagonIndexOffset = prismIndexOffset + (UINT)prism.Indices32.size();//hexagon
	UINT triangleEqIndexOffset = hexagonIndexOffset + (UINT)hexagon.Indices32.size();//TriangleEq
	UINT triangleRectSqrIndexOffset = triangleEqIndexOffset + (UINT)triangleEq.Indices32.size();//triangleRectSqr

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry diamondSubmesh;///////////// //4
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry pyramidSubmesh;/////////////
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry rhomboSubmesh;///////////// rhombo
	rhomboSubmesh.IndexCount = (UINT)rhombo.Indices32.size();
	rhomboSubmesh.StartIndexLocation = rhomboIndexOffset;
	rhomboSubmesh.BaseVertexLocation = rhomboVertexOffset;

	SubmeshGeometry prismSubmesh;///////////// prism
	prismSubmesh.IndexCount = (UINT)prism.Indices32.size();
	prismSubmesh.StartIndexLocation = prismIndexOffset;
	prismSubmesh.BaseVertexLocation = prismVertexOffset;

	SubmeshGeometry hexagonSubmesh;///////////// hexagon
	hexagonSubmesh.IndexCount = (UINT)hexagon.Indices32.size();
	hexagonSubmesh.StartIndexLocation = hexagonIndexOffset;
	hexagonSubmesh.BaseVertexLocation = hexagonVertexOffset;

	SubmeshGeometry triangleEqSubmesh;///////////// TriangleEq
	triangleEqSubmesh.IndexCount = (UINT)triangleEq.Indices32.size();
	triangleEqSubmesh.StartIndexLocation = triangleEqIndexOffset;
	triangleEqSubmesh.BaseVertexLocation = triangleEqVertexOffset;

	SubmeshGeometry triangleRectSqrSubmesh;//triangleRectSqr
	triangleRectSqrSubmesh.IndexCount = (UINT)triangleRectSqr.Indices32.size();
	triangleRectSqrSubmesh.StartIndexLocation = triangleRectSqrIndexOffset;
	triangleRectSqrSubmesh.BaseVertexLocation = triangleRectSqrVertexOffset;

	//
	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.
	//

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		diamond.Vertices.size() +
		pyramid.Vertices.size() +///////////// //5
		rhombo.Vertices.size() +//rhombo
		prism.Vertices.size() + //prism
		hexagon.Vertices.size() +  //hexagon
		triangleEq.Vertices.size() + //TriangleEq
		triangleRectSqr.Vertices.size();

	std::vector<Vertex> vertices(totalVertexCount);	

	UINT k = 0;
	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Normal = box.Vertices[i].Normal;
		vertices[k].TexC = box.Vertices[i].TexC;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Normal = grid.Vertices[i].Normal;
		vertices[k].TexC = grid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Normal = sphere.Vertices[i].Normal;
		vertices[k].TexC = sphere.Vertices[i].TexC;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
		vertices[k].TexC = cylinder.Vertices[i].TexC;
	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)///////////// //6
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Normal = diamond.Vertices[i].Normal;
		vertices[k].TexC = diamond.Vertices[i].TexC;
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)/////////////
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
		vertices[k].TexC = pyramid.Vertices[i].TexC;
	}

	for (size_t i = 0; i < rhombo.Vertices.size(); ++i, ++k)/////////////
	{
		vertices[k].Pos = rhombo.Vertices[i].Position;
		vertices[k].Normal = rhombo.Vertices[i].Normal;
		vertices[k].TexC = rhombo.Vertices[i].TexC;
	}

	for (size_t i = 0; i < prism.Vertices.size(); ++i, ++k)/////////////
	{
		vertices[k].Pos = prism.Vertices[i].Position;
		vertices[k].Normal = prism.Vertices[i].Normal;
		vertices[k].TexC = prism.Vertices[i].TexC;
	}

	for (size_t i = 0; i < hexagon.Vertices.size(); ++i, ++k)/////////////
	{
		vertices[k].Pos = hexagon.Vertices[i].Position;
		vertices[k].Normal = hexagon.Vertices[i].Normal;
		vertices[k].TexC = hexagon.Vertices[i].TexC;
	}

	for (size_t i = 0; i < triangleEq.Vertices.size(); ++i, ++k)/////////////
	{
		vertices[k].Pos = triangleEq.Vertices[i].Position;
		vertices[k].Normal = triangleEq.Vertices[i].Normal;
		vertices[k].TexC = triangleEq.Vertices[i].TexC;
	}

	for (size_t i = 0; i < triangleRectSqr.Vertices.size(); ++i, ++k)/////////////
	{
		vertices[k].Pos = triangleRectSqr.Vertices[i].Position;
		vertices[k].Normal = triangleRectSqr.Vertices[i].Normal;
		vertices[k].TexC = triangleRectSqr.Vertices[i].TexC;
	}

	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(rhombo.GetIndices16()), std::end(rhombo.GetIndices16()));///////////// //7
	indices.insert(indices.end(), std::begin(prism.GetIndices16()), std::end(prism.GetIndices16()));
	indices.insert(indices.end(), std::begin(hexagon.GetIndices16()), std::end(hexagon.GetIndices16()));
	indices.insert(indices.end(), std::begin(triangleEq.GetIndices16()), std::end(triangleEq.GetIndices16()));
	indices.insert(indices.end(), std::begin(triangleRectSqr.GetIndices16()), std::end(triangleRectSqr.GetIndices16()));

    const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT)indices.size()  * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh; ///////////// //8
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["rhombo"] = rhomboSubmesh;
	geo->DrawArgs["prism"] = prismSubmesh;
	geo->DrawArgs["hexagon"] = hexagonSubmesh;
	geo->DrawArgs["triangleEq"] = triangleEqSubmesh;
	geo->DrawArgs["triangleRectSqr"] = triangleRectSqrSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}

void CameraAndDynamicIndexingApp::BuildSkullGeometry()
{
	std::ifstream fin("Models/skull.txt");

	if (!fin)
	{
		MessageBox(0, L"Models/skull.txt not found.", 0, 0);
		return;
	}

	UINT vcount = 0;
	UINT tcount = 0;
	std::string ignore;

	fin >> ignore >> vcount;
	fin >> ignore >> tcount;
	fin >> ignore >> ignore >> ignore >> ignore;

	std::vector<Vertex> vertices(vcount);
	for (UINT i = 0; i < vcount; ++i)
	{
		fin >> vertices[i].Pos.x >> vertices[i].Pos.y >> vertices[i].Pos.z;
		fin >> vertices[i].Normal.x >> vertices[i].Normal.y >> vertices[i].Normal.z;
	}

	fin >> ignore;
	fin >> ignore;
	fin >> ignore;

	std::vector<std::int32_t> indices(3 * tcount);
	for (UINT i = 0; i < tcount; ++i)
	{
		fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
	}

	fin.close();

	//
	// Pack the indices of all the meshes into one index buffer.
	//

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);

	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::int32_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "skullGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R32_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["skull"] = submesh;

	mGeometries[geo->Name] = std::move(geo);
}

void CameraAndDynamicIndexingApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();
	opaquePsoDesc.VS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()), 
		mShaders["standardVS"]->GetBufferSize()
	};
	opaquePsoDesc.PS = 
	{ 
		reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
		mShaders["opaquePS"]->GetBufferSize()
	};
	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));
}

void CameraAndDynamicIndexingApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size()));
    }
}

void CameraAndDynamicIndexingApp::BuildMaterials()
{
	auto bricks0 = std::make_unique<Material>();
	bricks0->Name = "bricks0";
	bricks0->MatCBIndex = 0;
	bricks0->DiffuseSrvHeapIndex = 0;
	bricks0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    bricks0->Roughness = 0.1f;

	auto stone0 = std::make_unique<Material>();
	stone0->Name = "stone0";
	stone0->MatCBIndex = 1;
	stone0->DiffuseSrvHeapIndex = 1;
	stone0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    stone0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    stone0->Roughness = 0.3f;
 
	auto tile0 = std::make_unique<Material>();
	tile0->Name = "tile0";
	tile0->MatCBIndex = 2;
	tile0->DiffuseSrvHeapIndex = 2;
	tile0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    tile0->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
    tile0->Roughness = 0.3f;

	auto crate0 = std::make_unique<Material>();
	crate0->Name = "crate0";
	crate0->MatCBIndex = 3;
	crate0->DiffuseSrvHeapIndex = 3;
	crate0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    crate0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    crate0->Roughness = 0.2f;

	auto skullMat = std::make_unique<Material>();
	skullMat->Name = "skullMat";
	skullMat->MatCBIndex = 4;
	skullMat->DiffuseSrvHeapIndex = 4;
	skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05);
	skullMat->Roughness = 0.3f;

	auto eye0 = std::make_unique<Material>();
	eye0->Name = "eye0";
	eye0->MatCBIndex = 5;
	eye0->DiffuseSrvHeapIndex = 5;
	eye0->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);//XMFLOAT4(Colors::Blue);
	eye0->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	eye0->Roughness = 0.3f;

	auto eye1 = std::make_unique<Material>();
	eye1->Name = "eye1";
	eye1->MatCBIndex = 6;
	eye1->DiffuseSrvHeapIndex = 6;
	eye1->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);// XMFLOAT4(Colors::Black);
	eye1->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	eye1->Roughness = 0.3f;

	auto sun = std::make_unique<Material>();
	sun->Name = "sun";
	sun->MatCBIndex = 7;
	sun->DiffuseSrvHeapIndex = 7;
	sun->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);// XMFLOAT4(Colors::Yellow);
	sun->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	sun->Roughness = 0.3f;

	auto red = std::make_unique<Material>();
	red->Name = "red";
	red->MatCBIndex = 8;
	red->DiffuseSrvHeapIndex = 8;
	red->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);// XMFLOAT4(Colors::Red);
	red->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	red->Roughness = 0.3f;

	auto hex = std::make_unique<Material>();
	hex->Name = "hex";
	hex->MatCBIndex = 9;
	hex->DiffuseSrvHeapIndex = 9;
	hex->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);// XMFLOAT4(Colors::Orange);
	hex->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	hex->Roughness = 0.3f;

	auto trianglepurple = std::make_unique<Material>();
	trianglepurple->Name = "trianglepurple";
	trianglepurple->MatCBIndex = 10;
	trianglepurple->DiffuseSrvHeapIndex = 10;
	trianglepurple->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);// XMFLOAT4(Colors::Purple);
	trianglepurple->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
	trianglepurple->Roughness = 0.3f;

	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 11;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 12;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;
	
	mMaterials["bricks0"] = std::move(bricks0);
	mMaterials["stone0"] = std::move(stone0);
	mMaterials["tile0"] = std::move(tile0);
	mMaterials["crate0"] = std::move(crate0);
	mMaterials["skullMat"] = std::move(skullMat);
	mMaterials["eye0"] = std::move(eye0);
	mMaterials["eye1"] = std::move(eye1);
	mMaterials["sun"] = std::move(sun);
	mMaterials["red"] = std::move(red);
	mMaterials["hex"] = std::move(hex);
	mMaterials["grass"] = std::move(grass);
	
	mMaterials["trianglepurple"] = std::move(trianglepurple);
	mMaterials["water"] = std::move(water);
}

void CameraAndDynamicIndexingApp::BuildRenderItems()
{

	/*

	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f)*XMMatrixTranslation(0.0f, 1.0f, 0.0f));
	XMStoreFloat4x4(&boxRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	boxRitem->ObjCBIndex = 0;
	boxRitem->Mat = mMaterials["crate0"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(boxRitem));

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 1;
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
    gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));

	XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	UINT objCBIndex = 2;
	for(int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();
		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i*5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i*5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i*5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i*5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);
		XMStoreFloat4x4(&leftCylRitem->TexTransform, brickTexTransform);
		leftCylRitem->ObjCBIndex = objCBIndex++;
		leftCylRitem->Mat = mMaterials["bricks0"].get();
		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);
		XMStoreFloat4x4(&rightCylRitem->TexTransform, brickTexTransform);
		rightCylRitem->ObjCBIndex = objCBIndex++;
		rightCylRitem->Mat = mMaterials["bricks0"].get();
		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);
		leftSphereRitem->TexTransform = MathHelper::Identity4x4();
		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Mat = mMaterials["stone0"].get();
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);
		rightSphereRitem->TexTransform = MathHelper::Identity4x4();
		rightSphereRitem->ObjCBIndex = objCBIndex++;
		rightSphereRitem->Mat = mMaterials["stone0"].get();
		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));

		
	}
	*/


	auto basePillar = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&basePillar->World, XMMatrixScaling(4.0f, 6.0f, 4.0f)/* * XMMatrixRotationX(5.1) */* XMMatrixTranslation(0.0f, 5.0f, 0.0f));
	XMStoreFloat4x4(&basePillar->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	basePillar->ObjCBIndex = 0;
	basePillar->Mat = mMaterials["bricks0"].get();
	basePillar->Geo = mGeometries["shapeGeo"].get();
	basePillar->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	basePillar->IndexCount = basePillar->Geo->DrawArgs["cylinder"].IndexCount;
	basePillar->StartIndexLocation = basePillar->Geo->DrawArgs["cylinder"].StartIndexLocation;
	basePillar->BaseVertexLocation = basePillar->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mAllRitems.push_back(std::move(basePillar));
	
	/*auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem->ObjCBIndex = 1;
	gridRitem->Mat = mMaterials["tile0"].get();
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem));*/
	////////////////////////////////////////////
	/*
	//Skull issue.  Had to add the below item to replace it's ObjCBIndex position
	auto gridRitem2 = std::make_unique<RenderItem>();
	gridRitem2->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem2->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
	gridRitem2->ObjCBIndex = 2;
	gridRitem2->Mat = mMaterials["tile0"].get();
	gridRitem2->Geo = mGeometries["shapeGeo"].get();
	gridRitem2->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem2->IndexCount = gridRitem2->Geo->DrawArgs["grid"].IndexCount;
	gridRitem2->StartIndexLocation = gridRitem2->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem2->BaseVertexLocation = gridRitem2->Geo->DrawArgs["grid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(gridRitem2));
	*/
	auto skullRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f)*XMMatrixTranslation(0.0f, 14.0f, 0.0f));
	XMStoreFloat4x4(&skullRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = 2;
	skullRitem->Mat = mMaterials["skullMat"].get();
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["skull"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["skull"].BaseVertexLocation;
	mAllRitems.push_back(std::move(skullRitem));
	
	//XMMATRIX brickTexTransform = XMMatrixScaling(1.0f, 1.0f, 1.0f);
	
	auto diamondRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(5.0f, 5.0f, 5.0f) * XMMatrixRotationX(5.1) * XMMatrixTranslation(-0.7f, 15.9f, -0.6f));
	XMStoreFloat4x4(&diamondRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	diamondRitem->ObjCBIndex = 3;
	diamondRitem->Mat = mMaterials["eye1"].get();
	diamondRitem->Geo = mGeometries["shapeGeo"].get();
	diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamondRitem));

	auto diamond1Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamond1Ritem->World, XMMatrixScaling(5.0f, 5.0f, 5.0f) * XMMatrixRotationX(5.1) * XMMatrixTranslation(0.7f, 15.9f, -0.6f));
	XMStoreFloat4x4(&diamond1Ritem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	diamond1Ritem->ObjCBIndex = 4;
	diamond1Ritem->Mat = mMaterials["eye1"].get();
	diamond1Ritem->Geo = mGeometries["shapeGeo"].get();
	diamond1Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamond1Ritem->IndexCount = diamond1Ritem->Geo->DrawArgs["diamond"].IndexCount;
	diamond1Ritem->StartIndexLocation = diamond1Ritem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamond1Ritem->BaseVertexLocation = diamond1Ritem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamond1Ritem));

	auto pyramidRitem = std::make_unique<RenderItem>(); //9
	XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(15.0f, 18.0f, -15.0f));
	XMStoreFloat4x4(&pyramidRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidRitem->ObjCBIndex = 5;
	pyramidRitem->Mat = mMaterials["red"].get();
	pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidRitem));

	auto rhomboRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rhomboRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(6.7f, 8.0f, -17.0f));
	XMStoreFloat4x4(&rhomboRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rhomboRitem->ObjCBIndex = 6; //10
	rhomboRitem->Mat = mMaterials["eye0"].get();
	rhomboRitem->Geo = mGeometries["shapeGeo"].get();
	rhomboRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rhomboRitem->IndexCount = rhomboRitem->Geo->DrawArgs["rhombo"].IndexCount;
	rhomboRitem->StartIndexLocation = rhomboRitem->Geo->DrawArgs["rhombo"].StartIndexLocation;
	rhomboRitem->BaseVertexLocation = rhomboRitem->Geo->DrawArgs["rhombo"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rhomboRitem));



	auto sphereRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&sphereRitem->World, XMMatrixScaling(3.0f, 3.0f, 3.0f) * XMMatrixTranslation(-20.7f, 40.0f, 35.0f));
	XMStoreFloat4x4(&sphereRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	sphereRitem->ObjCBIndex = 7; //10
	sphereRitem->Mat = mMaterials["sun"].get();
	sphereRitem->Geo = mGeometries["shapeGeo"].get();
	sphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mAllRitems.push_back(std::move(sphereRitem));


	auto hexagonRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&hexagonRitem->World, XMMatrixScaling(3.0f, 0.1f, 3.0f)* XMMatrixTranslation(0.0f, 0.2f, 0.0f));
	XMStoreFloat4x4(&hexagonRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	hexagonRitem->ObjCBIndex = 8; //10
	hexagonRitem->Mat = mMaterials["hex"].get();
	hexagonRitem->Geo = mGeometries["shapeGeo"].get();
	hexagonRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	hexagonRitem->IndexCount = hexagonRitem->Geo->DrawArgs["hexagon"].IndexCount;
	hexagonRitem->StartIndexLocation = hexagonRitem->Geo->DrawArgs["hexagon"].StartIndexLocation;
	hexagonRitem->BaseVertexLocation = hexagonRitem->Geo->DrawArgs["hexagon"].BaseVertexLocation;
	mAllRitems.push_back(std::move(hexagonRitem));

	auto triangleEqRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleEqRitem->World, XMMatrixScaling(2.0f, 2.0f, 15.0f)* XMMatrixTranslation(-15.0f, 16.0f, -0.0f));
	XMStoreFloat4x4(&triangleEqRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleEqRitem->ObjCBIndex = 9; //10
	triangleEqRitem->Mat = mMaterials["trianglepurple"].get();
	triangleEqRitem->Geo = mGeometries["shapeGeo"].get();
	triangleEqRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleEqRitem->IndexCount = triangleEqRitem->Geo->DrawArgs["triangleEq"].IndexCount;
	triangleEqRitem->StartIndexLocation = triangleEqRitem->Geo->DrawArgs["triangleEq"].StartIndexLocation;
	triangleEqRitem->BaseVertexLocation = triangleEqRitem->Geo->DrawArgs["triangleEq"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triangleEqRitem));

	auto triangleRectSqrRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleRectSqrRitem->World, XMMatrixScaling(2.5f, 2.5f, 2.5f)* XMMatrixTranslation(12.0f, 13.5f, -15.0f));
	XMStoreFloat4x4(&triangleRectSqrRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleRectSqrRitem->ObjCBIndex = 10; //10
	triangleRectSqrRitem->Mat = mMaterials["skullMat"].get();
	triangleRectSqrRitem->Geo = mGeometries["shapeGeo"].get();
	triangleRectSqrRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleRectSqrRitem->IndexCount = triangleRectSqrRitem->Geo->DrawArgs["triangleRectSqr"].IndexCount;
	triangleRectSqrRitem->StartIndexLocation = triangleRectSqrRitem->Geo->DrawArgs["triangleRectSqr"].StartIndexLocation;
	triangleRectSqrRitem->BaseVertexLocation = triangleRectSqrRitem->Geo->DrawArgs["triangleRectSqr"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triangleRectSqrRitem));

	auto leftCastleWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftCastleWall->World, XMMatrixScaling(2.0f, 30.0f, 20.0f)*XMMatrixTranslation(-15.0f, 7.5f, 0.0f));
	XMStoreFloat4x4(&leftCastleWall->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	leftCastleWall->ObjCBIndex = 11;
	leftCastleWall->Mat = mMaterials["stone0"].get();
	leftCastleWall->Geo = mGeometries["shapeGeo"].get();
	leftCastleWall->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftCastleWall->IndexCount = leftCastleWall->Geo->DrawArgs["box"].IndexCount;
	leftCastleWall->StartIndexLocation = leftCastleWall->Geo->DrawArgs["box"].StartIndexLocation;
	leftCastleWall->BaseVertexLocation = leftCastleWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(leftCastleWall));


	auto rightCastleWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightCastleWall->World, XMMatrixScaling(2.0f, 30.0f, 20.0f)*XMMatrixTranslation(15.0f, 7.5f, 0.0f));
	XMStoreFloat4x4(&rightCastleWall->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rightCastleWall->ObjCBIndex = 12;
	rightCastleWall->Mat = mMaterials["stone0"].get();
	rightCastleWall->Geo = mGeometries["shapeGeo"].get();
	rightCastleWall->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightCastleWall->IndexCount = rightCastleWall->Geo->DrawArgs["box"].IndexCount;
	rightCastleWall->StartIndexLocation = rightCastleWall->Geo->DrawArgs["box"].StartIndexLocation;
	rightCastleWall->BaseVertexLocation = rightCastleWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rightCastleWall));

	auto backCastleWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&backCastleWall->World, XMMatrixScaling(22.0f, 24.0f, 2.0f)*XMMatrixTranslation(0.0f, 6.0f, 15.0f));
	XMStoreFloat4x4(&backCastleWall->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	backCastleWall->ObjCBIndex = 13;
	backCastleWall->Mat = mMaterials["stone0"].get();
	backCastleWall->Geo = mGeometries["shapeGeo"].get();
	backCastleWall->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backCastleWall->IndexCount = backCastleWall->Geo->DrawArgs["box"].IndexCount;
	backCastleWall->StartIndexLocation = backCastleWall->Geo->DrawArgs["box"].StartIndexLocation;
	backCastleWall->BaseVertexLocation = backCastleWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(backCastleWall));

	auto frontLeftCastleWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontLeftCastleWall->World, XMMatrixScaling(7.0f, 24.0f, 2.0f)*XMMatrixTranslation(-10.0f, 6.0f, -15.0f));
	XMStoreFloat4x4(&frontLeftCastleWall->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	frontLeftCastleWall->ObjCBIndex = 14;
	frontLeftCastleWall->Mat = mMaterials["stone0"].get();
	frontLeftCastleWall->Geo = mGeometries["shapeGeo"].get();
	frontLeftCastleWall->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontLeftCastleWall->IndexCount = frontLeftCastleWall->Geo->DrawArgs["box"].IndexCount;
	frontLeftCastleWall->StartIndexLocation = frontLeftCastleWall->Geo->DrawArgs["box"].StartIndexLocation;
	frontLeftCastleWall->BaseVertexLocation = frontLeftCastleWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontLeftCastleWall));

	auto frontRightCastleWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontRightCastleWall->World, XMMatrixScaling(7.0f, 24.0f, 2.0f)*XMMatrixTranslation(10.0f, 6.0f, -15.0f));
	XMStoreFloat4x4(&frontRightCastleWall->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	frontRightCastleWall->ObjCBIndex = 15;
	frontRightCastleWall->Mat = mMaterials["stone0"].get();
	frontRightCastleWall->Geo = mGeometries["shapeGeo"].get();
	frontRightCastleWall->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontRightCastleWall->IndexCount = frontRightCastleWall->Geo->DrawArgs["box"].IndexCount;
	frontRightCastleWall->StartIndexLocation = frontRightCastleWall->Geo->DrawArgs["box"].StartIndexLocation;
	frontRightCastleWall->BaseVertexLocation = frontRightCastleWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontRightCastleWall));

	auto frontRightCastlePillar = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontRightCastlePillar->World, XMMatrixScaling(2.0f, 40.0f, 2.0f)*XMMatrixTranslation(15.0f, 8.5f, -15.0f));
	XMStoreFloat4x4(&frontRightCastlePillar->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	frontRightCastlePillar->ObjCBIndex = 16;
	frontRightCastlePillar->Mat = mMaterials["stone0"].get();
	frontRightCastlePillar->Geo = mGeometries["shapeGeo"].get();
	frontRightCastlePillar->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontRightCastlePillar->IndexCount = frontRightCastlePillar->Geo->DrawArgs["box"].IndexCount;
	frontRightCastlePillar->StartIndexLocation = frontRightCastlePillar->Geo->DrawArgs["box"].StartIndexLocation;
	frontRightCastlePillar->BaseVertexLocation = frontRightCastlePillar->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontRightCastlePillar));



	auto frontLeftCastlePillar = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontLeftCastlePillar->World, XMMatrixScaling(2.0f, 40.0f, 2.0f)*XMMatrixTranslation(-15.0f, 8.5f, -15.0f));
	XMStoreFloat4x4(&frontLeftCastlePillar->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	frontLeftCastlePillar->ObjCBIndex = 17;
	frontLeftCastlePillar->Mat = mMaterials["stone0"].get();
	frontLeftCastlePillar->Geo = mGeometries["shapeGeo"].get();
	frontLeftCastlePillar->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontLeftCastlePillar->IndexCount = frontLeftCastlePillar->Geo->DrawArgs["box"].IndexCount;
	frontLeftCastlePillar->StartIndexLocation = frontLeftCastlePillar->Geo->DrawArgs["box"].StartIndexLocation;
	frontLeftCastlePillar->BaseVertexLocation = frontLeftCastlePillar->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontLeftCastlePillar));


	auto backLeftCastlePillar = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&backLeftCastlePillar->World, XMMatrixScaling(2.0f, 40.0f, 2.0f)*XMMatrixTranslation(-15.0f, 8.5f, 15.0f));
	XMStoreFloat4x4(&backLeftCastlePillar->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	backLeftCastlePillar->ObjCBIndex = 18;
	backLeftCastlePillar->Mat = mMaterials["stone0"].get();
	backLeftCastlePillar->Geo = mGeometries["shapeGeo"].get();
	backLeftCastlePillar->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backLeftCastlePillar->IndexCount = backLeftCastlePillar->Geo->DrawArgs["box"].IndexCount;
	backLeftCastlePillar->StartIndexLocation = backLeftCastlePillar->Geo->DrawArgs["box"].StartIndexLocation;
	backLeftCastlePillar->BaseVertexLocation = backLeftCastlePillar->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(backLeftCastlePillar));


	auto backRightCastlePillar = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&backRightCastlePillar->World, XMMatrixScaling(2.0f, 40.0f, 2.0f)*XMMatrixTranslation(15.0f, 8.5f, 15.0f));
	XMStoreFloat4x4(&backRightCastlePillar->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	backRightCastlePillar->ObjCBIndex = 19;
	backRightCastlePillar->Mat = mMaterials["stone0"].get();
	backRightCastlePillar->Geo = mGeometries["shapeGeo"].get();
	backRightCastlePillar->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backRightCastlePillar->IndexCount = backRightCastlePillar->Geo->DrawArgs["box"].IndexCount;
	backRightCastlePillar->StartIndexLocation = backRightCastlePillar->Geo->DrawArgs["box"].StartIndexLocation;
	backRightCastlePillar->BaseVertexLocation = backRightCastlePillar->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(backRightCastlePillar));


	auto frontCastleWallUp = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontCastleWallUp->World, XMMatrixScaling(22.0f, 8.0f, 2.0f)*XMMatrixTranslation(0.0f, 9.0f, -15.0f));
	XMStoreFloat4x4(&frontCastleWallUp->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	frontCastleWallUp->ObjCBIndex = 20;
	frontCastleWallUp->Mat = mMaterials["stone0"].get();
	frontCastleWallUp->Geo = mGeometries["shapeGeo"].get();
	frontCastleWallUp->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontCastleWallUp->IndexCount = frontCastleWallUp->Geo->DrawArgs["box"].IndexCount;
	frontCastleWallUp->StartIndexLocation = frontCastleWallUp->Geo->DrawArgs["box"].StartIndexLocation;
	frontCastleWallUp->BaseVertexLocation = frontCastleWallUp->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(frontCastleWallUp));

	auto triangleRectSqrBack = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleRectSqrBack->World, XMMatrixScaling(2.5f, 2.5f, 2.5f)* XMMatrixTranslation(12.0f, 13.5f, 15.0f));
	XMStoreFloat4x4(&triangleRectSqrBack->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleRectSqrBack->ObjCBIndex = 21;
	triangleRectSqrBack->Mat = mMaterials["skullMat"].get();
	triangleRectSqrBack->Geo = mGeometries["shapeGeo"].get();
	triangleRectSqrBack->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleRectSqrBack->IndexCount = triangleRectSqrBack->Geo->DrawArgs["triangleRectSqr"].IndexCount;
	triangleRectSqrBack->StartIndexLocation = triangleRectSqrBack->Geo->DrawArgs["triangleRectSqr"].StartIndexLocation;
	triangleRectSqrBack->BaseVertexLocation = triangleRectSqrBack->Geo->DrawArgs["triangleRectSqr"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triangleRectSqrBack));

	auto triangleRectSqrBackLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleRectSqrBackLeft->World, XMMatrixScaling(2.5f, 2.5f, 2.5f) * XMMatrixRotationY(3.12) * XMMatrixTranslation(-12.0f, 13.5f, 15.0f));
	XMStoreFloat4x4(&triangleRectSqrBackLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleRectSqrBackLeft->ObjCBIndex = 22;
	triangleRectSqrBackLeft->Mat = mMaterials["skullMat"].get();
	triangleRectSqrBackLeft->Geo = mGeometries["shapeGeo"].get();
	triangleRectSqrBackLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleRectSqrBackLeft->IndexCount = triangleRectSqrBackLeft->Geo->DrawArgs["triangleRectSqr"].IndexCount;
	triangleRectSqrBackLeft->StartIndexLocation = triangleRectSqrBackLeft->Geo->DrawArgs["triangleRectSqr"].StartIndexLocation;
	triangleRectSqrBackLeft->BaseVertexLocation = triangleRectSqrBackLeft->Geo->DrawArgs["triangleRectSqr"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triangleRectSqrBackLeft));

	auto triangleRectSqrFrontLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleRectSqrFrontLeft->World, XMMatrixScaling(2.5f, 2.5f, 2.5f) * XMMatrixRotationY(3.12)* XMMatrixTranslation(-12.0f, 13.5f, -15.0f));
	XMStoreFloat4x4(&triangleRectSqrFrontLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleRectSqrFrontLeft->ObjCBIndex = 23;
	triangleRectSqrFrontLeft->Mat = mMaterials["skullMat"].get();
	triangleRectSqrFrontLeft->Geo = mGeometries["shapeGeo"].get();
	triangleRectSqrFrontLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleRectSqrFrontLeft->IndexCount = triangleRectSqrFrontLeft->Geo->DrawArgs["triangleRectSqr"].IndexCount;
	triangleRectSqrFrontLeft->StartIndexLocation = triangleRectSqrFrontLeft->Geo->DrawArgs["triangleRectSqr"].StartIndexLocation;
	triangleRectSqrFrontLeft->BaseVertexLocation = triangleRectSqrFrontLeft->Geo->DrawArgs["triangleRectSqr"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triangleRectSqrFrontLeft));

	auto triangleright = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleright->World, XMMatrixScaling(2.0f, 2.0f, 15.0f)* XMMatrixTranslation(15.0f, 16.0f, -0.0f));
	XMStoreFloat4x4(&triangleright->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleright->ObjCBIndex = 24;
	triangleright->Mat = mMaterials["trianglepurple"].get();
	triangleright->Geo = mGeometries["shapeGeo"].get();
	triangleright->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleright->IndexCount = triangleright->Geo->DrawArgs["triangleEq"].IndexCount;
	triangleright->StartIndexLocation = triangleright->Geo->DrawArgs["triangleEq"].StartIndexLocation;
	triangleright->BaseVertexLocation = triangleright->Geo->DrawArgs["triangleEq"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triangleright));

	auto pyramidFrontLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidFrontLeft->World, XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(-15.0f, 18.0f, -15.0f));
	XMStoreFloat4x4(&pyramidFrontLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidFrontLeft->ObjCBIndex = 25;
	pyramidFrontLeft->Mat = mMaterials["red"].get();
	pyramidFrontLeft->Geo = mGeometries["shapeGeo"].get();
	pyramidFrontLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidFrontLeft->IndexCount = pyramidFrontLeft->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidFrontLeft->StartIndexLocation = pyramidFrontLeft->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidFrontLeft->BaseVertexLocation = pyramidFrontLeft->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidFrontLeft));

	auto pyramidBackLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidBackLeft->World, XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(-15.0f, 18.0f, 15.0f));
	XMStoreFloat4x4(&pyramidBackLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidBackLeft->ObjCBIndex = 26;
	pyramidBackLeft->Mat = mMaterials["red"].get();
	pyramidBackLeft->Geo = mGeometries["shapeGeo"].get();
	pyramidBackLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidBackLeft->IndexCount = pyramidBackLeft->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidBackLeft->StartIndexLocation = pyramidBackLeft->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidBackLeft->BaseVertexLocation = pyramidBackLeft->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidBackLeft));

	auto pyramidBackRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidBackRight->World, XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(15.0f, 18.0f, 15.0f));
	XMStoreFloat4x4(&pyramidBackRight->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidBackRight->ObjCBIndex = 27;
	pyramidBackRight->Mat = mMaterials["red"].get();
	pyramidBackRight->Geo = mGeometries["shapeGeo"].get();
	pyramidBackRight->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidBackRight->IndexCount = pyramidBackRight->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidBackRight->StartIndexLocation = pyramidBackRight->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidBackRight->BaseVertexLocation = pyramidBackRight->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidBackRight));

	auto rhomboLitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rhomboLitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(-6.7f, 8.0f, -17.0f));
	XMStoreFloat4x4(&rhomboLitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rhomboLitem->ObjCBIndex = 28;
	rhomboLitem->Mat = mMaterials["eye0"].get();
	rhomboLitem->Geo = mGeometries["shapeGeo"].get();
	rhomboLitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rhomboLitem->IndexCount = rhomboLitem->Geo->DrawArgs["rhombo"].IndexCount;
	rhomboLitem->StartIndexLocation = rhomboLitem->Geo->DrawArgs["rhombo"].StartIndexLocation;
	rhomboLitem->BaseVertexLocation = rhomboLitem->Geo->DrawArgs["rhombo"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rhomboLitem));

	auto prismRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&prismRitem->World, XMMatrixScaling(0.1f, 0.2f, 0.1f)* XMMatrixTranslation(0.0f, 20.0f, 0.0f));
	XMStoreFloat4x4(&prismRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	prismRitem->ObjCBIndex = 29; //10
	prismRitem->Mat = mMaterials["red"].get();
	prismRitem->Geo = mGeometries["shapeGeo"].get();
	prismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	prismRitem->IndexCount = prismRitem->Geo->DrawArgs["prism"].IndexCount;
	prismRitem->StartIndexLocation = prismRitem->Geo->DrawArgs["prism"].StartIndexLocation;
	prismRitem->BaseVertexLocation = prismRitem->Geo->DrawArgs["prism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(prismRitem));

	auto wavesRitem = std::make_unique<RenderItem>();
	wavesRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	wavesRitem->ObjCBIndex = 30;
	wavesRitem->Mat = mMaterials["water"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mWavesRitem = wavesRitem.get();

	mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());

	auto gridRitem = std::make_unique<RenderItem>();
	gridRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	gridRitem->ObjCBIndex = 1;
	gridRitem->Mat = mMaterials["grass"].get();
	gridRitem->Geo = mGeometries["landGeo"].get();
	gridRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem.get());

	for(auto& e : mAllRitems)
		mOpaqueRitems.push_back(e.get());
}

void CameraAndDynamicIndexingApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
 
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;

		// CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		// tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

		cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> CameraAndDynamicIndexingApp::GetStaticSamplers()
{
	// Applications usually only need a handful of samplers.  So just define them all up front
	// and keep them available as part of the root signature.  

	const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_POINT, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3, // shaderRegister
		D3D12_FILTER_MIN_MAG_MIP_LINEAR, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressW
		0.0f,                             // mipLODBias
		8);                               // maxAnisotropy

	const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5, // shaderRegister
		D3D12_FILTER_ANISOTROPIC, // filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressW
		0.0f,                              // mipLODBias
		8);                                // maxAnisotropy

	return { 
		pointWrap, pointClamp,
		linearWrap, linearClamp, 
		anisotropicWrap, anisotropicClamp };
}

float CameraAndDynamicIndexingApp::GetHillsHeight(float x, float z)const
{
	return 0.3f*(z*sinf(0.1f*x) + x * cosf(0.1f*z));
}

XMFLOAT3 CameraAndDynamicIndexingApp::GetHillsNormal(float x, float z)const
{
	// n = (-df/dx, 1, -df/dz)
	XMFLOAT3 n(
		-0.03f*z*cosf(0.1f*x) - 0.3f*cosf(0.1f*z),
		1.0f,
		-0.3f*sinf(0.1f*x) + 0.03f*x*sinf(0.1f*z));

	XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
	XMStoreFloat3(&n, unitNormal);

	return n;
}

