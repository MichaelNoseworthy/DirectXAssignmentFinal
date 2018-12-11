//***************************************************************************************
// DirectXAssignmentFinalApp.cpp by Frank Luna (C) 2015 All Rights Reserved.
//***************************************************************************************

#include "Common/d3dApp.h"
#include "Common/Camera.h"
#include "Common/MathHelper.h"
#include "Common/UploadBuffer.h"
#include "Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "Waves.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "D3D12.lib")

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

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
		AlphaTestedTreeSprites,
		Count
	};

class DirectXAssignmentFinalApp : public D3DApp
{
public:
    DirectXAssignmentFinalApp(HINSTANCE hInstance);
    DirectXAssignmentFinalApp(const DirectXAssignmentFinalApp& rhs) = delete;
    DirectXAssignmentFinalApp& operator=(const DirectXAssignmentFinalApp& rhs) = delete;
    ~DirectXAssignmentFinalApp();

    virtual bool Initialize()override;

private:
    virtual void OnResize()override;
    virtual void Update(const GameTimer& gt)override;
    virtual void Draw(const GameTimer& gt)override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

    void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void AnimateMaterials(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMaterialCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);
	void UpdateWaves(const GameTimer& gt);

	void LoadTextures();
    void BuildRootSignature();
	void BuildDescriptorHeaps();
    void BuildShadersAndInputLayouts();
    void BuildLandGeometry();
    void BuildWavesGeometry();
	void BuildBoxGeometry();
	void BuildSkullGeometry();
	void BuildTreeSpritesGeometry();
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

    std::vector<D3D12_INPUT_ELEMENT_DESC> mStdInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

    RenderItem* mWavesRitem = nullptr;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];

	std::unique_ptr<Waves> mWaves;

    PassConstants mMainPassCB;

	XMFLOAT3 mEyePos = { 0.0f, 0.0f, 0.0f };
	XMFLOAT4X4 mView = MathHelper::Identity4x4();
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

   /* float mTheta = 1.5f*XM_PI;
    float mPhi = XM_PIDIV2 - 0.1f;
    float mRadius = 50.0f;*/

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
        DirectXAssignmentFinalApp theApp(hInstance);
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

DirectXAssignmentFinalApp::DirectXAssignmentFinalApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{
}

DirectXAssignmentFinalApp::~DirectXAssignmentFinalApp()
{
    if(md3dDevice != nullptr)
        FlushCommandQueue();
}

bool DirectXAssignmentFinalApp::Initialize()
{
    if(!D3DApp::Initialize())
        return false;

    // Reset the command list to prep for initialization commands.
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    // Get the increment size of a descriptor in this heap type.  This is hardware specific,
	// so we have to query this information.
    mCbvSrvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mCamera.SetPosition(0.0f, 22.0f, -60.0f);

    mWaves = std::make_unique<Waves>(128, 128, 5.0f, 0.03f, 4.0f, 0.2f);

	LoadTextures();
    BuildRootSignature();
	BuildDescriptorHeaps();
    BuildShadersAndInputLayouts();
    BuildLandGeometry();
    BuildWavesGeometry();
	BuildBoxGeometry();
	BuildSkullGeometry();
	BuildTreeSpritesGeometry();
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

void DirectXAssignmentFinalApp::OnResize()
{
    D3DApp::OnResize();

    // The window resized, so update the aspect ratio and recompute the projection matrix.
	/*
    XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, P);*/
	mCamera.SetLens(0.25f*MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
}

void DirectXAssignmentFinalApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
	UpdateCamera(gt);

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
	UpdateMaterialCBs(gt);
	UpdateMainPassCB(gt);
    UpdateWaves(gt);
}

void DirectXAssignmentFinalApp::Draw(const GameTimer& gt)
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

	DirectX::XMFLOAT4 clearColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    // Clear the back buffer and depth buffer.
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), (float*)&clearColor, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // Specify the buffers we are going to render to.
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mSrvDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	auto passCB = mCurrFrameResource->PassCB->Resource();
	mCommandList->SetGraphicsRootConstantBufferView(2, passCB->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Opaque]);

	mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTested]);

	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);

	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::Transparent]);

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

void DirectXAssignmentFinalApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void DirectXAssignmentFinalApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void DirectXAssignmentFinalApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f*static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f*static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
       /* mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);*/

		mCamera.Pitch(dy);
		mCamera.RotateY(dx);
    }
   /* else if((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.2 unit in the scene.
        float dx = 0.2f*static_cast<float>(x - mLastMousePos.x);
        float dy = 0.2f*static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }*/

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void DirectXAssignmentFinalApp::OnKeyboardInput(const GameTimer& gt)
{

	const float dt = gt.DeltaTime();

	if (GetAsyncKeyState('W') & 0x8000)
		mCamera.Walk(20.0f*dt);

	if (GetAsyncKeyState('S') & 0x8000)
		mCamera.Walk(-20.0f*dt);

	if (GetAsyncKeyState('A') & 0x8000)
		mCamera.Strafe(-20.0f*dt);

	if (GetAsyncKeyState('D') & 0x8000)
		mCamera.Strafe(20.0f*dt);

	if (GetAsyncKeyState('Q') & 0x8000)
	{
		float dr = XMConvertToRadians(0.5f*static_cast<float>(80.0f * dt));
		mCamera.Roll(dr);
	}

	if (GetAsyncKeyState('E') & 0x8000)
	{
		float dr = XMConvertToRadians(0.5f*static_cast<float>(-80.0f * dt));
		mCamera.Roll(dr);
	}

	mCamera.UpdateViewMatrix();

}

void DirectXAssignmentFinalApp::UpdateCamera(const GameTimer& gt)
{
	/*
	// Convert Spherical to Cartesian coordinates.
	mEyePos.x = mRadius*sinf(mPhi)*cosf(mTheta);
	mEyePos.z = mRadius*sinf(mPhi)*sinf(mTheta);
	mEyePos.y = mRadius*cosf(mPhi);

	// Build the view matrix.
	XMVECTOR pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
	XMVECTOR target = XMVectorZero();
	XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

	XMMATRIX view = XMMatrixLookAtLH(pos, target, up);
	XMStoreFloat4x4(&mView, view);*/
}

void DirectXAssignmentFinalApp::AnimateMaterials(const GameTimer& gt)
{
	// Scroll the water material texture coordinates.
	auto waterMat = mMaterials["water"].get();

	float& tu = waterMat->MatTransform(3, 0);
	float& tv = waterMat->MatTransform(3, 1);

	tu += 0.1f * gt.DeltaTime();
	tv += 0.02f * gt.DeltaTime();

	if(tu >= 1.0f)
		tu -= 1.0f;

	if(tv >= 1.0f)
		tv -= 1.0f;

	waterMat->MatTransform(3, 0) = tu;
	waterMat->MatTransform(3, 1) = tv;

	// Material has changed, so need to update cbuffer.
	waterMat->NumFramesDirty = gNumFrameResources;
}

void DirectXAssignmentFinalApp::UpdateObjectCBs(const GameTimer& gt)
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

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void DirectXAssignmentFinalApp::UpdateMaterialCBs(const GameTimer& gt)
{
	auto currMaterialCB = mCurrFrameResource->MaterialCB.get();
	for(auto& e : mMaterials)
	{
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if(mat->NumFramesDirty > 0)
		{
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConstants;
			matConstants.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConstants.FresnelR0 = mat->FresnelR0;
			matConstants.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConstants.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB->CopyData(mat->MatCBIndex, matConstants);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}
}

void DirectXAssignmentFinalApp::UpdateMainPassCB(const GameTimer& gt)
{
	/*XMMATRIX view = XMLoadFloat4x4(&mView);
	XMMATRIX proj = XMLoadFloat4x4(&mProj);*/

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

	//inner castle light
	mMainPassCB.Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[0].Strength = { 0.6f, 0.6f, 0.6f };
	//mMainPassCB.Lights[0].Strength = { 0.0f, 0.0f, 0.0f };

	//Directional Light
	mMainPassCB.Lights[1].Direction = { -0.57735f, -0.57735f, 0.57735f };
	mMainPassCB.Lights[1].Strength = { 0.3f, 0.3f, 0.3f };
	//mMainPassCB.Lights[1].Strength = { 0.0f, 0.0f, 0.0f };


	//Directional Light  Red
	mMainPassCB.Lights[2].Direction = { 0.0f, -0.707f, -0.707f };
	mMainPassCB.Lights[2].Strength = { 1.0f, -0.5f, -0.55f };
	//mMainPassCB.Lights[2].Strength = { 0.0f, -0.0f, -0.0f };


	//Point light //blue Skull
	mMainPassCB.Lights[3].Position = { 0.0f, 29.5f, -2.0f };
	mMainPassCB.Lights[3].FalloffStart = 0.0f;
	mMainPassCB.Lights[3].Strength = { 0.0f, 0.0f, 4.0f };
	//mMainPassCB.Lights[3].Strength = { 0.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[3].FalloffEnd = 10.f;

	//Point light Sun
	mMainPassCB.Lights[4].Position = { -20.7f, 50.0f, 30.0 };
	mMainPassCB.Lights[4].FalloffStart = 0.0f;
	mMainPassCB.Lights[4].Strength = { 0.93f*2.f, 0.99f*2.f, 4.f };
	//mMainPassCB.Lights[4].Strength = { 0.0f, 0.0f, 0.f };
	mMainPassCB.Lights[4].FalloffEnd = 10.f;

	////5
	mMainPassCB.Lights[5].Position = { 5.0f, 14.5f, 8.0f };
	mMainPassCB.Lights[5].FalloffStart = 0.0f;
	mMainPassCB.Lights[5].Strength = { 0.0f, 0.0f, 4.0f };
	mMainPassCB.Lights[5].FalloffEnd = 10.f;

	//
	////Spot light On Left Tower Front
	mMainPassCB.Lights[5].FalloffStart = 0.0f;
	mMainPassCB.Lights[5].Strength = { 2.0f, 2.0f, 2.0f };
	//mMainPassCB.Lights[5].Strength = { 0.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[5].FalloffEnd = 10.f;
	mMainPassCB.Lights[5].Direction = { -26.0f, 0.0f, -75.0f };
	mMainPassCB.Lights[5].SpotPower = 0.8f;
	mMainPassCB.Lights[5].Position = { -3.f, 17.0f, -17.f };

	////Spot light On right Tower 
	mMainPassCB.Lights[6].FalloffStart = 0.0f;
	mMainPassCB.Lights[6].Strength = { 1.3f, 1.3f, 1.3f };
	//mMainPassCB.Lights[6].Strength = { 0.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[6].FalloffEnd = 10.f;
	mMainPassCB.Lights[6].Direction = { 15.0f, 0.0f, -17.0f };
	mMainPassCB.Lights[6].SpotPower = 1.0f;
	mMainPassCB.Lights[6].Position = { 7.0f, 17.0f, 0.f };

	////Spot light On Left Tower
	mMainPassCB.Lights[7].FalloffStart = 0.0f;
	mMainPassCB.Lights[7].Strength = { 1.3f, 1.3f, 1.3f };
	//mMainPassCB.Lights[7].Strength = { 0.0f, 0.0f, 0.0f };
	mMainPassCB.Lights[7].FalloffEnd = 10.f;
	mMainPassCB.Lights[7].Direction = { -15.0f, 0.0f, -17.0f };
	mMainPassCB.Lights[7].SpotPower = 1.0f;
	mMainPassCB.Lights[7].Position = { -7.0f, 17.0f, 0.f };

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void DirectXAssignmentFinalApp::UpdateWaves(const GameTimer& gt)
{
	// Every quarter second, generate a random wave.
	static float t_base = 0.0f;
	if((mTimer.TotalTime() - t_base) >= 0.25f)
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
	for(int i = 0; i < mWaves->VertexCount(); ++i)
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

void DirectXAssignmentFinalApp::LoadTextures()
{
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

	auto fenceTex = std::make_unique<Texture>();
	fenceTex->Name = "fenceTex";
	fenceTex->Filename = L"Textures/mossy.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), fenceTex->Filename.c_str(),
		fenceTex->Resource, fenceTex->UploadHeap));

	auto bricksTex = std::make_unique<Texture>();
	bricksTex->Name = "bricksTex";
	bricksTex->Filename = L"Textures/bricks3.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), bricksTex->Filename.c_str(),
		bricksTex->Resource, bricksTex->UploadHeap));

	auto iceTex = std::make_unique<Texture>();
	iceTex->Name = "iceTex";
	iceTex->Filename = L"Textures/ice.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), iceTex->Filename.c_str(),
		iceTex->Resource, iceTex->UploadHeap));

	auto stoneTex = std::make_unique<Texture>();
	stoneTex->Name = "stoneTex";
	stoneTex->Filename = L"Textures/stone.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), stoneTex->Filename.c_str(),
		stoneTex->Resource, stoneTex->UploadHeap));

	auto pyramidTex = std::make_unique<Texture>();
	pyramidTex->Name = "pyramidTex";
	pyramidTex->Filename = L"Textures/pyramid.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), pyramidTex->Filename.c_str(),
		pyramidTex->Resource, pyramidTex->UploadHeap));

	auto sunTex = std::make_unique<Texture>();//777
	sunTex->Name = "sunTex";
	sunTex->Filename = L"Textures/sun.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), sunTex->Filename.c_str(),
		sunTex->Resource, sunTex->UploadHeap));

	auto mossyTex = std::make_unique<Texture>();
	mossyTex->Name = "mossyTex";
	mossyTex->Filename = L"Textures/mossy.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), mossyTex->Filename.c_str(),
		mossyTex->Resource, mossyTex->UploadHeap));

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"Textures/treeArray2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));


	mTextures[grassTex->Name] = std::move(grassTex);
	mTextures[waterTex->Name] = std::move(waterTex);
	mTextures[fenceTex->Name] = std::move(fenceTex);
	mTextures[bricksTex->Name] = std::move(bricksTex);
	mTextures[iceTex->Name] = std::move(iceTex);
	mTextures[stoneTex->Name] = std::move(stoneTex);
	mTextures[pyramidTex->Name] = std::move(pyramidTex);
	mTextures[sunTex->Name] = std::move(sunTex);
	mTextures[mossyTex->Name] = std::move(mossyTex);
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
}

void DirectXAssignmentFinalApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    // Root parameter can be a table, root descriptor or root constants.
    CD3DX12_ROOT_PARAMETER slotRootParameter[4];

	// Perfomance TIP: Order from most frequent to least frequent.
	slotRootParameter[0].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);
    slotRootParameter[1].InitAsConstantBufferView(0);
    slotRootParameter[2].InitAsConstantBufferView(1);
    slotRootParameter[3].InitAsConstantBufferView(2);

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

void DirectXAssignmentFinalApp::BuildDescriptorHeaps()
{
	//
	// Create the SRV heap.
	//
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc = {};
	srvHeapDesc.NumDescriptors = 9;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSrvDescriptorHeap)));

	//
	// Fill out the heap with actual descriptors.
	//
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mSrvDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

	auto grassTex = mTextures["grassTex"]->Resource;
	auto waterTex = mTextures["waterTex"]->Resource;
	auto fenceTex = mTextures["fenceTex"]->Resource;
	auto bricksTex = mTextures["bricksTex"]->Resource;
	auto iceTex = mTextures["iceTex"]->Resource;
	auto stoneTex = mTextures["stoneTex"]->Resource;
	auto pyramidTex = mTextures["pyramidTex"]->Resource;;
	auto sunTex = mTextures["sunTex"]->Resource;
	auto mossyTex = mTextures["mossyTex"]->Resource;
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;

	// srv 0
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = grassTex->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = -1;
	md3dDevice->CreateShaderResourceView(grassTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	// srv 1
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = waterTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(waterTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	// srv 2
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = fenceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(fenceTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	// srv 3
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = bricksTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(bricksTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	// srv4
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = iceTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(iceTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	// srv 5
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = stoneTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(stoneTex.Get(), &srvDesc, hDescriptor);

	// next descriptor
	// srv 6
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = pyramidTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(pyramidTex.Get(), &srvDesc, hDescriptor);
	
	//suntex
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = sunTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(sunTex.Get(), &srvDesc, hDescriptor);

	//mossyTex
	hDescriptor.Offset(1, mCbvSrvDescriptorSize);
	srvDesc.Format = mossyTex->GetDesc().Format;
	md3dDevice->CreateShaderResourceView(mossyTex.Get(), &srvDesc, hDescriptor);

	auto desc = treeArrayTex->GetDesc();
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);
}

void DirectXAssignmentFinalApp::BuildShadersAndInputLayouts()
{
	const D3D_SHADER_MACRO defines[] =
	{
		"FOG", "1",
		NULL, NULL
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"FOG", "1",
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", defines, "PS", "ps_5_0");
	mShaders["alphaTestedPS"] = d3dUtil::CompileShader(L"Shaders\\Default.hlsl", alphaTestDefines, "PS", "ps_5_0");

	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_0");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_0");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_0");

    mStdInputLayout =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };

	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}

void DirectXAssignmentFinalApp::BuildLandGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(600.0f, 600.0f, 50, 50);

    //
    // Extract the vertex elements we are interested and apply the height function to
    // each vertex.  In addition, color the vertices based on their height so we have
    // sandy looking beaches, grassy low hills, and snow mountain peaks.
    //

    std::vector<Vertex> vertices(grid.Vertices.size());
    for(size_t i = 0; i < grid.Vertices.size(); ++i)
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

void DirectXAssignmentFinalApp::BuildWavesGeometry()
{
    std::vector<std::uint16_t> indices(3 * mWaves->TriangleCount()); // 3 indices per face
	assert(mWaves->VertexCount() < 0x0000ffff);

    // Iterate over each quad.
    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for(int i = 0; i < m - 1; ++i)
    {
        for(int j = 0; j < n - 1; ++j)
        {
            indices[k] = i*n + j;
            indices[k + 1] = i*n + j + 1;
            indices[k + 2] = (i + 1)*n + j;

            indices[k + 3] = (i + 1)*n + j;
            indices[k + 4] = i*n + j + 1;
            indices[k + 5] = (i + 1)*n + j + 1;

            k += 6; // next quad
        }
    }

	UINT vbByteSize = mWaves->VertexCount()*sizeof(Vertex);
	UINT ibByteSize = (UINT)indices.size()*sizeof(std::uint16_t);

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

void DirectXAssignmentFinalApp::BuildBoxGeometry()
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
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);


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

void DirectXAssignmentFinalApp::BuildSkullGeometry()
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


void DirectXAssignmentFinalApp::BuildTreeSpritesGeometry()
{
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	static const int treeCount = 20;
	std::array<TreeSpriteVertex, 40> vertices;
	float x = 35.0f;
	float z = 35.0f;
	for(UINT i = 0; i < treeCount; ++i)
	{
		float x = MathHelper::RandF(MathHelper::RandF(-80.f, -135.0), MathHelper::RandF(25.f, 65.0));
		float z = MathHelper::RandF(MathHelper::RandF(-80.f, -135.0), MathHelper::RandF(25.f, 65.0));

		float y = GetHillsHeight(x, z);

		// Move tree slightly above land height.
		y += 4.0f;

		vertices[i].Pos = XMFLOAT3(x, y, z);
		vertices[i].Size = XMFLOAT2(20.0f, 20.0f);
		vertices[i+1].Pos = XMFLOAT3(x, y, -z);
		vertices[i+1].Size = XMFLOAT2(20.0f, 20.0f);
		i ++;
		x -= 5.0f;
	}
	x = 25.f;
	for (UINT j = 20; j < treeCount*2; ++j)
	{
		/*float x = MathHelper::RandF(MathHelper::RandF(-60.f, -25.0), MathHelper::RandF(25.f, 65.0));
		float z = MathHelper::RandF(MathHelper::RandF(-60.f, -25.0), MathHelper::RandF(25.f, 65.0));*/

		float y = GetHillsHeight(x, z);

		// Move tree slightly above land height.
		y += 4.0f;

		vertices[j].Pos = XMFLOAT3(x, y, z);
		vertices[j].Size = XMFLOAT2(10.0f, 10.0f);

		vertices[j + 1].Pos = XMFLOAT3(-x, y, z);
		vertices[j + 1].Size = XMFLOAT2(10.0f, 10.0f);
		j++;
		z -= 5.0f;
	}

	std::array<std::uint16_t, 40> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19,
		20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;

	geo->DrawArgs["points"] = submesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);
}

void DirectXAssignmentFinalApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	//
	// PSO for opaque objects.
	//
    ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mStdInputLayout.data(), (UINT)mStdInputLayout.size() };
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

	//
	// PSO for transparent objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

	D3D12_RENDER_TARGET_BLEND_DESC transparencyBlendDesc;
	transparencyBlendDesc.BlendEnable = true;
	transparencyBlendDesc.LogicOpEnable = false;
	transparencyBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparencyBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparencyBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
	transparencyBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
	transparencyBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparencyBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
	transparencyBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPsoDesc.BlendState.RenderTarget[0] = transparencyBlendDesc;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	//
	// PSO for alpha tested objects
	//

	D3D12_GRAPHICS_PIPELINE_STATE_DESC alphaTestedPsoDesc = opaquePsoDesc;
	alphaTestedPsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["alphaTestedPS"]->GetBufferPointer()),
		mShaders["alphaTestedPS"]->GetBufferSize()
	};
	alphaTestedPsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&alphaTestedPsoDesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));

	//
	// PSO for tree sprites
	//
	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
	treeSpritePsoDesc.VS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()),
		mShaders["treeSpriteVS"]->GetBufferSize()
	};
	treeSpritePsoDesc.GS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()),
		mShaders["treeSpriteGS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PS =
	{
		reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()),
		mShaders["treeSpritePS"]->GetBufferSize()
	};
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));
}

void DirectXAssignmentFinalApp::BuildFrameResources()
{
    for(int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
            1, (UINT)mAllRitems.size(), (UINT)mMaterials.size(), mWaves->VertexCount()));
    }
}

void DirectXAssignmentFinalApp::BuildMaterials()
{
	int cbIndex = 8;
	int srvHeapIndex = 8;

	auto grass = std::make_unique<Material>();
	grass->Name = "grass";
	grass->MatCBIndex = 0;
	grass->DiffuseSrvHeapIndex = 0;
	grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
	grass->Roughness = 0.125f;

	// This is not a good water material definition, but we do not have all the rendering
	// tools we need (transparency, environment reflection), so we fake it for now.
	auto water = std::make_unique<Material>();
	water->Name = "water";
	water->MatCBIndex = 1;
	water->DiffuseSrvHeapIndex = 1;
	water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
	water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
	water->Roughness = 0.0f;

	auto wirefence = std::make_unique<Material>();
	wirefence->Name = "wirefence";
	wirefence->MatCBIndex = 2;
	wirefence->DiffuseSrvHeapIndex = 2;
	wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	wirefence->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	wirefence->Roughness = 0.25f;

	auto treeSprites = std::make_unique<Material>();
	treeSprites->Name = "treeSprites";
	treeSprites->MatCBIndex = 3;
	treeSprites->DiffuseSrvHeapIndex = 8;
	treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);

	auto bricks = std::make_unique<Material>();
	bricks->Name = "bricks";
	bricks->MatCBIndex = 4;
	bricks->DiffuseSrvHeapIndex = 3;
	bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	bricks->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	bricks->Roughness = 0.25f;

	auto ice = std::make_unique<Material>();
	ice->Name = "ice";
	ice->MatCBIndex = 5;
	ice->DiffuseSrvHeapIndex = 4;
	ice->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	ice->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	ice->Roughness = 0.25f;

	auto stone = std::make_unique<Material>();//9999
	stone->Name = "stone";
	stone->MatCBIndex = 6;
	stone->DiffuseSrvHeapIndex = 5;
	stone->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	stone->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	stone->Roughness = 0.25f;

	auto pyramid = std::make_unique<Material>();
	pyramid->Name = "pyramid";
	pyramid->MatCBIndex = 7;
	pyramid->DiffuseSrvHeapIndex = 6;
	pyramid->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	pyramid->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	pyramid->Roughness = 0.25f;

	auto sunMat = std::make_unique<Material>();
	sunMat->Name = "sunMat";
	sunMat->MatCBIndex = 8;
	sunMat->DiffuseSrvHeapIndex = 7;
	sunMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	sunMat->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	sunMat->Roughness = 0.25f;

	auto mossy = std::make_unique<Material>();
	mossy->Name = "mossy";
	mossy->MatCBIndex = 9;
	mossy->DiffuseSrvHeapIndex = 8;
	mossy->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	mossy->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	mossy->Roughness = 0.25f;

	auto stonestep = std::make_unique<Material>();
	stonestep->Name = "stonestep";
	stonestep->MatCBIndex = 10;
	stonestep->DiffuseSrvHeapIndex = 9;
	stonestep->DiffuseAlbedo = XMFLOAT4(Colors::Gray);
	stonestep->FresnelR0 = XMFLOAT3(0.02f, 0.02f, 0.02f);
	stonestep->Roughness = 0.01f;

	
	mMaterials["grass"] = std::move(grass);
	mMaterials["water"] = std::move(water);
	mMaterials["wirefence"] = std::move(wirefence);
	mMaterials["treeSprites"] = std::move(treeSprites);
	mMaterials["bricks"] = std::move(bricks);
	mMaterials["ice"] = std::move(ice);
	mMaterials["stone"] = std::move(stone);
	mMaterials["pyramid"] = std::move(pyramid);
	mMaterials["sunMat"] = std::move(sunMat);
	mMaterials["mossy"] = std::move(mossy);
	mMaterials["stonestep"] = std::move(stonestep);

}

void DirectXAssignmentFinalApp::BuildRenderItems()
{

	UINT objCBIndex = 0;

	float yLevel = 10;
	
    auto wavesRitem = std::make_unique<RenderItem>();
    wavesRitem->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&wavesRitem->TexTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
	wavesRitem->ObjCBIndex = 0;
	wavesRitem->Mat = mMaterials["water"].get();
	wavesRitem->Geo = mGeometries["waterGeo"].get();
	wavesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	wavesRitem->IndexCount = wavesRitem->Geo->DrawArgs["grid"].IndexCount;
	wavesRitem->StartIndexLocation = wavesRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	wavesRitem->BaseVertexLocation = wavesRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	//Just the waves.
    mWavesRitem = wavesRitem.get();
	mRitemLayer[(int)RenderLayer::Transparent].push_back(wavesRitem.get());
	mAllRitems.push_back(std::move(wavesRitem));

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
	
	auto boxRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&boxRitem->World, XMMatrixTranslation(3.0f, 2.0f, -9.0f));
	boxRitem->ObjCBIndex = 2;
	boxRitem->Mat = mMaterials["wirefence"].get();
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTested].push_back(boxRitem.get());
	
	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
	treeSpritesRitem->ObjCBIndex = 3;
	treeSpritesRitem->Mat = mMaterials["treeSprites"].get();
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	treeSpritesRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;

	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());
	

	mAllRitems.push_back(std::move(gridRitem));
	mAllRitems.push_back(std::move(boxRitem));
	mAllRitems.push_back(std::move(treeSpritesRitem));
	
	auto basePillar = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&basePillar->World, XMMatrixScaling(4.0f, 6.0f, 4.0f) * XMMatrixTranslation(0.0f, yLevel + 5.0f, 0.0f));
	XMStoreFloat4x4(&basePillar->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	basePillar->ObjCBIndex = 4;
	basePillar->Mat = mMaterials["stone"].get();
	basePillar->Geo = mGeometries["shapeGeo"].get();
	basePillar->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	basePillar->IndexCount = basePillar->Geo->DrawArgs["cylinder"].IndexCount;
	basePillar->StartIndexLocation = basePillar->Geo->DrawArgs["cylinder"].StartIndexLocation;
	basePillar->BaseVertexLocation = basePillar->Geo->DrawArgs["cylinder"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(basePillar.get());
	mAllRitems.push_back(std::move(basePillar));
	
	auto gridRitem3 = std::make_unique<RenderItem>();
	gridRitem3->World = MathHelper::Identity4x4();
	XMStoreFloat4x4(&gridRitem3->TexTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f) * XMMatrixTranslation(0.0f, yLevel + 0.0f, 0.0f));
	gridRitem3->ObjCBIndex = 5;
	gridRitem3->Mat = mMaterials["stone"].get();
	gridRitem3->Geo = mGeometries["shapeGeo"].get();
	gridRitem3->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	gridRitem3->IndexCount = gridRitem3->Geo->DrawArgs["grid"].IndexCount;
	gridRitem3->StartIndexLocation = gridRitem3->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem3->BaseVertexLocation = gridRitem3->Geo->DrawArgs["grid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(gridRitem3.get());
	mAllRitems.push_back(std::move(gridRitem3));
	
	auto diamondRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondRitem->World, XMMatrixScaling(5.0f, 5.0f, 5.0f) * XMMatrixRotationX(5.1) * XMMatrixTranslation(-0.7f, yLevel + 15.9f, -0.6f));
	XMStoreFloat4x4(&diamondRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	diamondRitem->ObjCBIndex = 6;
	diamondRitem->Mat = mMaterials["ice"].get();
	diamondRitem->Geo = mGeometries["shapeGeo"].get();
	diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(diamondRitem.get());
	mAllRitems.push_back(std::move(diamondRitem));
	
	auto diamond1Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamond1Ritem->World, XMMatrixScaling(5.0f, 5.0f, 5.0f) * XMMatrixRotationX(5.1) * XMMatrixTranslation(0.7f, yLevel + 15.9f, -0.6f));
	XMStoreFloat4x4(&diamond1Ritem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	diamond1Ritem->ObjCBIndex = 7;
	diamond1Ritem->Mat = mMaterials["ice"].get();
	diamond1Ritem->Geo = mGeometries["shapeGeo"].get();
	diamond1Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamond1Ritem->IndexCount = diamond1Ritem->Geo->DrawArgs["diamond"].IndexCount;
	diamond1Ritem->StartIndexLocation = diamond1Ritem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamond1Ritem->BaseVertexLocation = diamond1Ritem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(diamond1Ritem.get());
	mAllRitems.push_back(std::move(diamond1Ritem));
	
	auto pyramidRitem = std::make_unique<RenderItem>(); //9
	XMStoreFloat4x4(&pyramidRitem->World, XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(15.0f, yLevel + 18.0f, -15.0f));
	XMStoreFloat4x4(&pyramidRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidRitem->ObjCBIndex = 8;
	pyramidRitem->Mat = mMaterials["bricks"].get();
	pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramidRitem.get());
	mAllRitems.push_back(std::move(pyramidRitem));

	auto rhomboRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rhomboRitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(6.7f, yLevel + 8.0f, -17.0f));
	XMStoreFloat4x4(&rhomboRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rhomboRitem->ObjCBIndex = 9;
	rhomboRitem->Mat = mMaterials["pyramid"].get();
	rhomboRitem->Geo = mGeometries["shapeGeo"].get();
	rhomboRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rhomboRitem->IndexCount = rhomboRitem->Geo->DrawArgs["rhombo"].IndexCount;
	rhomboRitem->StartIndexLocation = rhomboRitem->Geo->DrawArgs["rhombo"].StartIndexLocation;
	rhomboRitem->BaseVertexLocation = rhomboRitem->Geo->DrawArgs["rhombo"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(rhomboRitem.get());
	mAllRitems.push_back(std::move(rhomboRitem));

	auto sphereRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&sphereRitem->World, XMMatrixScaling(3.0f, 3.0f, 3.0f) * XMMatrixTranslation(-20.7f, yLevel + 40.0f, 35.0f));
	XMStoreFloat4x4(&sphereRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	sphereRitem->ObjCBIndex = 10;
	sphereRitem->Mat = mMaterials["sunMat"].get();//sol
	sphereRitem->Geo = mGeometries["shapeGeo"].get();
	sphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;
	sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
	sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(sphereRitem.get());
	mAllRitems.push_back(std::move(sphereRitem));

	auto hexagonRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&hexagonRitem->World, XMMatrixScaling(3.0f, 0.1f, 3.0f)* XMMatrixTranslation(0.0f, yLevel, -5.0f));
	XMStoreFloat4x4(&hexagonRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	hexagonRitem->ObjCBIndex = 11;
	hexagonRitem->Mat = mMaterials["mossy"].get();
	hexagonRitem->Geo = mGeometries["shapeGeo"].get();
	hexagonRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	hexagonRitem->IndexCount = hexagonRitem->Geo->DrawArgs["hexagon"].IndexCount;
	hexagonRitem->StartIndexLocation = hexagonRitem->Geo->DrawArgs["hexagon"].StartIndexLocation;
	hexagonRitem->BaseVertexLocation = hexagonRitem->Geo->DrawArgs["hexagon"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(hexagonRitem.get());
	mAllRitems.push_back(std::move(hexagonRitem));

	auto triangleEqRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleEqRitem->World, XMMatrixScaling(2.0f, 2.0f, 15.0f)* XMMatrixTranslation(-15.0f, yLevel + 16.0f, -0.0f));
	XMStoreFloat4x4(&triangleEqRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleEqRitem->ObjCBIndex = 12;
	triangleEqRitem->Mat = mMaterials["bricks"].get();
	triangleEqRitem->Geo = mGeometries["shapeGeo"].get();
	triangleEqRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleEqRitem->IndexCount = triangleEqRitem->Geo->DrawArgs["triangleEq"].IndexCount;
	triangleEqRitem->StartIndexLocation = triangleEqRitem->Geo->DrawArgs["triangleEq"].StartIndexLocation;
	triangleEqRitem->BaseVertexLocation = triangleEqRitem->Geo->DrawArgs["triangleEq"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(triangleEqRitem.get());
	mAllRitems.push_back(std::move(triangleEqRitem));

	auto triangleRectSqrRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleRectSqrRitem->World, XMMatrixScaling(2.5f, 2.5f, 2.5f)* XMMatrixTranslation(12.0f, yLevel + 13.5f, -15.0f));
	XMStoreFloat4x4(&triangleRectSqrRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleRectSqrRitem->ObjCBIndex = 13; 
	triangleRectSqrRitem->Mat = mMaterials["bricks"].get();
	triangleRectSqrRitem->Geo = mGeometries["shapeGeo"].get();
	triangleRectSqrRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleRectSqrRitem->IndexCount = triangleRectSqrRitem->Geo->DrawArgs["triangleRectSqr"].IndexCount;
	triangleRectSqrRitem->StartIndexLocation = triangleRectSqrRitem->Geo->DrawArgs["triangleRectSqr"].StartIndexLocation;
	triangleRectSqrRitem->BaseVertexLocation = triangleRectSqrRitem->Geo->DrawArgs["triangleRectSqr"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(triangleRectSqrRitem.get());
	mAllRitems.push_back(std::move(triangleRectSqrRitem));

	auto leftCastleWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&leftCastleWall->World, XMMatrixScaling(2.0f, 30.0f, 20.0f)*XMMatrixTranslation(-15.0f, yLevel + 7.5f, 0.0f));
	XMStoreFloat4x4(&leftCastleWall->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	leftCastleWall->ObjCBIndex = 14;
	leftCastleWall->Mat = mMaterials["stone"].get();
	leftCastleWall->Geo = mGeometries["shapeGeo"].get();
	leftCastleWall->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	leftCastleWall->IndexCount = leftCastleWall->Geo->DrawArgs["box"].IndexCount;
	leftCastleWall->StartIndexLocation = leftCastleWall->Geo->DrawArgs["box"].StartIndexLocation;
	leftCastleWall->BaseVertexLocation = leftCastleWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(leftCastleWall.get());
	mAllRitems.push_back(std::move(leftCastleWall));

	auto rightCastleWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rightCastleWall->World, XMMatrixScaling(2.0f, 30.0f, 20.0f)*XMMatrixTranslation(15.0f, yLevel + 7.5f, 0.0f));
	XMStoreFloat4x4(&rightCastleWall->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rightCastleWall->ObjCBIndex = 15;
	rightCastleWall->Mat = mMaterials["stone"].get();
	rightCastleWall->Geo = mGeometries["shapeGeo"].get();
	rightCastleWall->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rightCastleWall->IndexCount = rightCastleWall->Geo->DrawArgs["box"].IndexCount;
	rightCastleWall->StartIndexLocation = rightCastleWall->Geo->DrawArgs["box"].StartIndexLocation;
	rightCastleWall->BaseVertexLocation = rightCastleWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(rightCastleWall.get());
	mAllRitems.push_back(std::move(rightCastleWall));

	auto backCastleWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&backCastleWall->World, XMMatrixScaling(22.0f, 24.0f, 2.0f)*XMMatrixTranslation(0.0f, yLevel + 6.0f, 15.0f));
	XMStoreFloat4x4(&backCastleWall->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	backCastleWall->ObjCBIndex = 16;
	backCastleWall->Mat = mMaterials["stone"].get();
	backCastleWall->Geo = mGeometries["shapeGeo"].get();
	backCastleWall->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backCastleWall->IndexCount = backCastleWall->Geo->DrawArgs["box"].IndexCount;
	backCastleWall->StartIndexLocation = backCastleWall->Geo->DrawArgs["box"].StartIndexLocation;
	backCastleWall->BaseVertexLocation = backCastleWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(backCastleWall.get());
	mAllRitems.push_back(std::move(backCastleWall));

	auto frontLeftCastleWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontLeftCastleWall->World, XMMatrixScaling(7.0f, 24.0f, 2.0f)*XMMatrixTranslation(-10.0f, yLevel + 6.0f, -15.0f));
	XMStoreFloat4x4(&frontLeftCastleWall->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	frontLeftCastleWall->ObjCBIndex = 17;
	frontLeftCastleWall->Mat = mMaterials["stone"].get();
	frontLeftCastleWall->Geo = mGeometries["shapeGeo"].get();
	frontLeftCastleWall->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontLeftCastleWall->IndexCount = frontLeftCastleWall->Geo->DrawArgs["box"].IndexCount;
	frontLeftCastleWall->StartIndexLocation = frontLeftCastleWall->Geo->DrawArgs["box"].StartIndexLocation;
	frontLeftCastleWall->BaseVertexLocation = frontLeftCastleWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(frontLeftCastleWall.get());
	mAllRitems.push_back(std::move(frontLeftCastleWall));

	auto frontRightCastleWall = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontRightCastleWall->World, XMMatrixScaling(7.0f, 24.0f, 2.0f)*XMMatrixTranslation(10.0f, yLevel + 6.0f, -15.0f));
	XMStoreFloat4x4(&frontRightCastleWall->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	frontRightCastleWall->ObjCBIndex = 18;
	frontRightCastleWall->Mat = mMaterials["stone"].get();
	frontRightCastleWall->Geo = mGeometries["shapeGeo"].get();
	frontRightCastleWall->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontRightCastleWall->IndexCount = frontRightCastleWall->Geo->DrawArgs["box"].IndexCount;
	frontRightCastleWall->StartIndexLocation = frontRightCastleWall->Geo->DrawArgs["box"].StartIndexLocation;
	frontRightCastleWall->BaseVertexLocation = frontRightCastleWall->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(frontRightCastleWall.get());
	mAllRitems.push_back(std::move(frontRightCastleWall));

	auto frontRightCastlePillar = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontRightCastlePillar->World, XMMatrixScaling(2.0f, 40.0f, 2.0f)*XMMatrixTranslation(15.1f, yLevel + 8.5f, -15.1f));
	XMStoreFloat4x4(&frontRightCastlePillar->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	frontRightCastlePillar->ObjCBIndex = 19;
	frontRightCastlePillar->Mat = mMaterials["stone"].get();
	frontRightCastlePillar->Geo = mGeometries["shapeGeo"].get();
	frontRightCastlePillar->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontRightCastlePillar->IndexCount = frontRightCastlePillar->Geo->DrawArgs["box"].IndexCount;
	frontRightCastlePillar->StartIndexLocation = frontRightCastlePillar->Geo->DrawArgs["box"].StartIndexLocation;
	frontRightCastlePillar->BaseVertexLocation = frontRightCastlePillar->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(frontRightCastlePillar.get());
	mAllRitems.push_back(std::move(frontRightCastlePillar));

	auto frontLeftCastlePillar = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontLeftCastlePillar->World, XMMatrixScaling(2.0f, 40.0f, 2.0f)*XMMatrixTranslation(-15.1f, yLevel + 8.5f, -15.1f));
	XMStoreFloat4x4(&frontLeftCastlePillar->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	frontLeftCastlePillar->ObjCBIndex = 20;
	frontLeftCastlePillar->Mat = mMaterials["stone"].get();
	frontLeftCastlePillar->Geo = mGeometries["shapeGeo"].get();
	frontLeftCastlePillar->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontLeftCastlePillar->IndexCount = frontLeftCastlePillar->Geo->DrawArgs["box"].IndexCount;
	frontLeftCastlePillar->StartIndexLocation = frontLeftCastlePillar->Geo->DrawArgs["box"].StartIndexLocation;
	frontLeftCastlePillar->BaseVertexLocation = frontLeftCastlePillar->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(frontLeftCastlePillar.get());
	mAllRitems.push_back(std::move(frontLeftCastlePillar));


	auto backLeftCastlePillar = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&backLeftCastlePillar->World, XMMatrixScaling(2.0f, 40.0f, 2.0f)*XMMatrixTranslation(-15.0f, yLevel + 8.5f, 15.0f));
	XMStoreFloat4x4(&backLeftCastlePillar->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	backLeftCastlePillar->ObjCBIndex = 21;
	backLeftCastlePillar->Mat = mMaterials["stone"].get();
	backLeftCastlePillar->Geo = mGeometries["shapeGeo"].get();
	backLeftCastlePillar->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backLeftCastlePillar->IndexCount = backLeftCastlePillar->Geo->DrawArgs["box"].IndexCount;
	backLeftCastlePillar->StartIndexLocation = backLeftCastlePillar->Geo->DrawArgs["box"].StartIndexLocation;
	backLeftCastlePillar->BaseVertexLocation = backLeftCastlePillar->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(backLeftCastlePillar.get());
	mAllRitems.push_back(std::move(backLeftCastlePillar));


	auto backRightCastlePillar = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&backRightCastlePillar->World, XMMatrixScaling(2.0f, 40.0f, 2.0f)*XMMatrixTranslation(15.0f, yLevel + 8.5f, 15.0f));
	XMStoreFloat4x4(&backRightCastlePillar->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	backRightCastlePillar->ObjCBIndex = 22;
	backRightCastlePillar->Mat = mMaterials["stone"].get();
	backRightCastlePillar->Geo = mGeometries["shapeGeo"].get();
	backRightCastlePillar->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	backRightCastlePillar->IndexCount = backRightCastlePillar->Geo->DrawArgs["box"].IndexCount;
	backRightCastlePillar->StartIndexLocation = backRightCastlePillar->Geo->DrawArgs["box"].StartIndexLocation;
	backRightCastlePillar->BaseVertexLocation = backRightCastlePillar->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(backRightCastlePillar.get());
	mAllRitems.push_back(std::move(backRightCastlePillar));


	auto frontCastleWallUp = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&frontCastleWallUp->World, XMMatrixScaling(22.0f, 8.0f, 1.5f)*XMMatrixTranslation(0.0f, yLevel + 9.0f, -15.0f));
	XMStoreFloat4x4(&frontCastleWallUp->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	frontCastleWallUp->ObjCBIndex = 23;
	frontCastleWallUp->Mat = mMaterials["stone"].get();
	frontCastleWallUp->Geo = mGeometries["shapeGeo"].get();
	frontCastleWallUp->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	frontCastleWallUp->IndexCount = frontCastleWallUp->Geo->DrawArgs["box"].IndexCount;
	frontCastleWallUp->StartIndexLocation = frontCastleWallUp->Geo->DrawArgs["box"].StartIndexLocation;
	frontCastleWallUp->BaseVertexLocation = frontCastleWallUp->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(frontCastleWallUp.get());
	mAllRitems.push_back(std::move(frontCastleWallUp));

	auto triangleRectSqrBack = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleRectSqrBack->World, XMMatrixScaling(2.5f, 2.5f, 2.5f)* XMMatrixTranslation(12.0f, yLevel + 13.5f, 15.0f));
	XMStoreFloat4x4(&triangleRectSqrBack->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleRectSqrBack->ObjCBIndex = 24;
	triangleRectSqrBack->Mat = mMaterials["bricks"].get();
	triangleRectSqrBack->Geo = mGeometries["shapeGeo"].get();
	triangleRectSqrBack->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleRectSqrBack->IndexCount = triangleRectSqrBack->Geo->DrawArgs["triangleRectSqr"].IndexCount;
	triangleRectSqrBack->StartIndexLocation = triangleRectSqrBack->Geo->DrawArgs["triangleRectSqr"].StartIndexLocation;
	triangleRectSqrBack->BaseVertexLocation = triangleRectSqrBack->Geo->DrawArgs["triangleRectSqr"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(triangleRectSqrBack.get());
	mAllRitems.push_back(std::move(triangleRectSqrBack));

	auto triangleRectSqrBackLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleRectSqrBackLeft->World, XMMatrixScaling(2.5f, 2.5f, 2.5f) * XMMatrixRotationY(3.12) * XMMatrixTranslation(-12.0f, yLevel + 13.5f, 15.0f));
	XMStoreFloat4x4(&triangleRectSqrBackLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleRectSqrBackLeft->ObjCBIndex = 25;
	triangleRectSqrBackLeft->Mat = mMaterials["bricks"].get();
	triangleRectSqrBackLeft->Geo = mGeometries["shapeGeo"].get();
	triangleRectSqrBackLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleRectSqrBackLeft->IndexCount = triangleRectSqrBackLeft->Geo->DrawArgs["triangleRectSqr"].IndexCount;
	triangleRectSqrBackLeft->StartIndexLocation = triangleRectSqrBackLeft->Geo->DrawArgs["triangleRectSqr"].StartIndexLocation;
	triangleRectSqrBackLeft->BaseVertexLocation = triangleRectSqrBackLeft->Geo->DrawArgs["triangleRectSqr"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(triangleRectSqrBackLeft.get());
	mAllRitems.push_back(std::move(triangleRectSqrBackLeft));

	auto triangleRectSqrFrontLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleRectSqrFrontLeft->World, XMMatrixScaling(2.5f, 2.5f, 2.5f) * XMMatrixRotationY(3.12)* XMMatrixTranslation(-12.0f, yLevel + 13.5f, -15.0f));
	XMStoreFloat4x4(&triangleRectSqrFrontLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleRectSqrFrontLeft->ObjCBIndex = 26;
	triangleRectSqrFrontLeft->Mat = mMaterials["bricks"].get();
	triangleRectSqrFrontLeft->Geo = mGeometries["shapeGeo"].get();
	triangleRectSqrFrontLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleRectSqrFrontLeft->IndexCount = triangleRectSqrFrontLeft->Geo->DrawArgs["triangleRectSqr"].IndexCount;
	triangleRectSqrFrontLeft->StartIndexLocation = triangleRectSqrFrontLeft->Geo->DrawArgs["triangleRectSqr"].StartIndexLocation;
	triangleRectSqrFrontLeft->BaseVertexLocation = triangleRectSqrFrontLeft->Geo->DrawArgs["triangleRectSqr"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(triangleRectSqrFrontLeft.get());
	mAllRitems.push_back(std::move(triangleRectSqrFrontLeft));

	auto triangleright = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangleright->World, XMMatrixScaling(2.0f, 2.0f, 15.0f)* XMMatrixTranslation(15.0f, yLevel + 16.0f, -0.0f));
	XMStoreFloat4x4(&triangleright->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	triangleright->ObjCBIndex = 27;
	triangleright->Mat = mMaterials["bricks"].get();
	triangleright->Geo = mGeometries["shapeGeo"].get();
	triangleright->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangleright->IndexCount = triangleright->Geo->DrawArgs["triangleEq"].IndexCount;
	triangleright->StartIndexLocation = triangleright->Geo->DrawArgs["triangleEq"].StartIndexLocation;
	triangleright->BaseVertexLocation = triangleright->Geo->DrawArgs["triangleEq"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(triangleright.get());
	mAllRitems.push_back(std::move(triangleright));

	auto pyramidFrontLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidFrontLeft->World, XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(-15.0f, yLevel + 18.0f, -15.0f));
	XMStoreFloat4x4(&pyramidFrontLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidFrontLeft->ObjCBIndex = 28;
	pyramidFrontLeft->Mat = mMaterials["bricks"].get();
	pyramidFrontLeft->Geo = mGeometries["shapeGeo"].get();
	pyramidFrontLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidFrontLeft->IndexCount = pyramidFrontLeft->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidFrontLeft->StartIndexLocation = pyramidFrontLeft->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidFrontLeft->BaseVertexLocation = pyramidFrontLeft->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramidFrontLeft.get());
	mAllRitems.push_back(std::move(pyramidFrontLeft));

	auto pyramidBackLeft = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidBackLeft->World, XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(-15.0f, yLevel + 18.0f, 15.0f));
	XMStoreFloat4x4(&pyramidBackLeft->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidBackLeft->ObjCBIndex = 29;
	pyramidBackLeft->Mat = mMaterials["bricks"].get();
	pyramidBackLeft->Geo = mGeometries["shapeGeo"].get();
	pyramidBackLeft->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidBackLeft->IndexCount = pyramidBackLeft->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidBackLeft->StartIndexLocation = pyramidBackLeft->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidBackLeft->BaseVertexLocation = pyramidBackLeft->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramidBackLeft.get());
	mAllRitems.push_back(std::move(pyramidBackLeft));

	auto pyramidBackRight = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidBackRight->World, XMMatrixScaling(4.0f, 4.0f, 4.0f)* XMMatrixTranslation(15.0f, yLevel + 18.0f, 15.0f));
	XMStoreFloat4x4(&pyramidBackRight->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	pyramidBackRight->ObjCBIndex = 30;
	pyramidBackRight->Mat = mMaterials["bricks"].get();
	pyramidBackRight->Geo = mGeometries["shapeGeo"].get();
	pyramidBackRight->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidBackRight->IndexCount = pyramidBackRight->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidBackRight->StartIndexLocation = pyramidBackRight->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidBackRight->BaseVertexLocation = pyramidBackRight->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(pyramidBackRight.get());
	mAllRitems.push_back(std::move(pyramidBackRight));

	auto rhomboLitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rhomboLitem->World, XMMatrixScaling(1.0f, 1.0f, 1.0f)* XMMatrixTranslation(-6.7f, yLevel + 8.0f, -17.0f));
	XMStoreFloat4x4(&rhomboLitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	rhomboLitem->ObjCBIndex = 31;
	rhomboLitem->Mat = mMaterials["pyramid"].get();//888
	rhomboLitem->Geo = mGeometries["shapeGeo"].get();
	rhomboLitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rhomboLitem->IndexCount = rhomboLitem->Geo->DrawArgs["rhombo"].IndexCount;
	rhomboLitem->StartIndexLocation = rhomboLitem->Geo->DrawArgs["rhombo"].StartIndexLocation;
	rhomboLitem->BaseVertexLocation = rhomboLitem->Geo->DrawArgs["rhombo"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(rhomboLitem.get());
	mAllRitems.push_back(std::move(rhomboLitem));

	auto prismRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&prismRitem->World, XMMatrixScaling(0.1f, 0.2f, 0.1f)* XMMatrixTranslation(0.0f, yLevel + 20.0f, 0.0f));
	XMStoreFloat4x4(&prismRitem->TexTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
	prismRitem->ObjCBIndex = 32;
	prismRitem->Mat = mMaterials["pyramid"].get();
	prismRitem->Geo = mGeometries["shapeGeo"].get();
	prismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	prismRitem->IndexCount = prismRitem->Geo->DrawArgs["prism"].IndexCount;
	prismRitem->StartIndexLocation = prismRitem->Geo->DrawArgs["prism"].StartIndexLocation;
	prismRitem->BaseVertexLocation = prismRitem->Geo->DrawArgs["prism"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(prismRitem.get());
	mAllRitems.push_back(std::move(prismRitem));
	
	//Skull
	auto skullRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&skullRitem->World, XMMatrixScaling(0.5f, 0.5f, 0.5f)*XMMatrixTranslation(0.0f, yLevel + 14.0f, 0.0f));
	skullRitem->TexTransform = MathHelper::Identity4x4();
	skullRitem->ObjCBIndex = 33;
	skullRitem->Mat = mMaterials["stone"].get();
	skullRitem->Geo = mGeometries["skullGeo"].get();
	skullRitem->PrimitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	skullRitem->IndexCount = skullRitem->Geo->DrawArgs["skull"].IndexCount;
	skullRitem->StartIndexLocation = skullRitem->Geo->DrawArgs["box"].StartIndexLocation;
	skullRitem->BaseVertexLocation = skullRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::Opaque].push_back(skullRitem.get());
	mAllRitems.push_back(std::move(skullRitem));
    
}

void DirectXAssignmentFinalApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
    UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    UINT matCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	auto matCB = mCurrFrameResource->MaterialCB->Resource();

    // For each render item...
    for(size_t i = 0; i < ritems.size(); ++i)
    {
        auto ri = ritems[i];

        cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
        cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
        cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mSrvDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(ri->Mat->DiffuseSrvHeapIndex, mCbvSrvDescriptorSize);

        D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB->GetGPUVirtualAddress() + ri->ObjCBIndex*objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB->GetGPUVirtualAddress() + ri->Mat->MatCBIndex*matCBByteSize;

		cmdList->SetGraphicsRootDescriptorTable(0, tex);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, matCBAddress);

        cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> DirectXAssignmentFinalApp::GetStaticSamplers()
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

float DirectXAssignmentFinalApp::GetHillsHeight(float x, float z)const
{
	return (40 * (cosf(x / 1600 * 7) + cosf(z / 1600 * 7))) - 70;
}

XMFLOAT3 DirectXAssignmentFinalApp::GetHillsNormal(float x, float z)const
{
    // n = (-df/dx, 1, -df/dz)
    XMFLOAT3 n(
		(7 / 40) * sinf((7 * x) / 1600), // no idea why this doesn't work
		1.0f,
		(7 / 40) * sinf((7 * z) / 1600)); // this should really work

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}
