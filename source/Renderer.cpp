#include "pch.h"
#include "Renderer.h"

#define DEBUG

namespace dae {

	Renderer::Renderer(SDL_Window* pWindow) :
		m_pWindow(pWindow)
	{
		//Initialize
		SDL_GetWindowSize(pWindow, &m_Width, &m_Height);
		m_pCamera = new Camera{};
		m_pCamera->Initialize(((float)m_Width) / ((float)m_Height), 45.f, { 0,0,-50 });

		//Initialize DirectX pipeline
		const HRESULT result = InitializeDirectX();
		if (result == S_OK)
		{
			m_IsInitialized = true;
			std::cout << "DirectX is initialized and ready!\n";
		}
		else
		{
			std::cout << "DirectX initialization failed!\n";
		}

		CreateMesh();

		//Initialize sampler
		m_pEffectSamplerVariable = m_pMesh->GetSampleVar();
		if (!m_pEffectSamplerVariable->IsValid())
		{
			std::wcout << L"m_pEffectSamplerVariable not valid!\n";
		}

		//Create sampler
		m_SamplerDesc.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
		m_SamplerDesc.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
		m_SamplerDesc.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;

		m_SamplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
		m_SamplerDesc.MipLODBias = 0;
		m_SamplerDesc.MinLOD = 0;

		m_SamplerDesc.MaxLOD = D3D11_FLOAT32_MAX;
		m_SamplerDesc.MaxAnisotropy = 16;
		PressFilterMethod();
	}

	Renderer::~Renderer()
	{
		if (m_pRenderTargetView)
		{
			m_pRenderTargetView->Release();
		}
		if (m_pRenderTargetBuffer)
		{
			m_pRenderTargetBuffer->Release();
		}
		if (m_pDepthStencilView)
		{
			m_pDepthStencilView->Release();
		}
		if (m_pDepthStencilBuffer)
		{
			m_pDepthStencilBuffer->Release();
		}
		if (m_pSwapChain)
		{
			m_pSwapChain->Release();
		}
		if (m_pDeviceContext)
		{
			m_pDeviceContext->ClearState();
			m_pDeviceContext->Flush();
			m_pDeviceContext->Release();
		}
		if (m_pDevice)
		{
			m_pDevice->Release();
		}
		if (m_pEffectSamplerVariable)
		{
			m_pEffectSamplerVariable->Release();
		}
		if (m_pSamplerState)
		{
			m_pSamplerState->Release();
		}

		delete m_pMesh;
		delete m_pCamera;
	}

	void Renderer::Update(const Timer* pTimer)
	{
		m_pCamera->Update(pTimer);

		const float rotateSpeed{ 45.0f };
		m_pMesh->RotateY(rotateSpeed * TO_RADIANS * pTimer->GetElapsed());

		m_pMesh->SetMatrix(m_pCamera->viewMatrix * m_pCamera->projectionMatrix);
	}


	void Renderer::Render() const
	{
		if (!m_IsInitialized)
			return;

		//Clear window for next frame
		ColorRGB clearColor{ 0.0f, 0.0f, 0.3f };
		m_pDeviceContext->ClearRenderTargetView(m_pRenderTargetView, &clearColor.r);
		m_pDeviceContext->ClearDepthStencilView(m_pDepthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		//Set pipeline + invoke drawcalls (= render)
		m_pMesh->Render(m_pDeviceContext);

		//Present to screen
		m_pSwapChain->Present(0, 0);
	}

	HRESULT Renderer::InitializeDirectX()
	{
		///1. Create Device & DeviceContext
		D3D_FEATURE_LEVEL featureLevel = D3D_FEATURE_LEVEL_11_1;
		uint32_t createDeviceFlags = 0;
#if defined(DEBUG) || defined(_DEBUG)
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
		HRESULT result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, createDeviceFlags, &featureLevel, 1, D3D11_SDK_VERSION, &m_pDevice, nullptr, &m_pDeviceContext);
		if (FAILED(result))
		{
			return result;
		}

		IDXGIFactory1* pDxgiFactory{ nullptr };
		result = CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&pDxgiFactory));
		if (FAILED(result))
		{
			return result;
		}

		///2. Create swapchain

		//Set properties of swapchain
		DXGI_SWAP_CHAIN_DESC swapChainDesc{};
		swapChainDesc.BufferDesc.Width = m_Width;
		swapChainDesc.BufferDesc.Height = m_Height;
		swapChainDesc.BufferDesc.RefreshRate.Numerator = 1;
		swapChainDesc.BufferDesc.RefreshRate.Denominator = 60;
		swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		swapChainDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
		swapChainDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
		swapChainDesc.SampleDesc.Count = 1;
		swapChainDesc.SampleDesc.Quality = 0;
		swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
		swapChainDesc.BufferCount = 1;
		swapChainDesc.Windowed = true;
		swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
		swapChainDesc.Flags = 0;

		//Get the handle (HWND) from the SDL Backbuffer
		SDL_SysWMinfo sysWMInfo{};
		SDL_VERSION(&sysWMInfo.version);
		SDL_GetWindowWMInfo(m_pWindow, &sysWMInfo);
		swapChainDesc.OutputWindow = sysWMInfo.info.win.window;

		//Create swapchain
		result = pDxgiFactory->CreateSwapChain(m_pDevice, &swapChainDesc, &m_pSwapChain);
		if (FAILED(result))
		{
			return result;
		}

		///3. Create DepthStencil (DS) & DepthStencilView (DSV)

		//Set properties of depthstencil
		D3D11_TEXTURE2D_DESC depthStencilDesc{};
		depthStencilDesc.Width = m_Width;
		depthStencilDesc.Height = m_Height;
		depthStencilDesc.MipLevels = 1;
		depthStencilDesc.ArraySize = 1;
		depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		depthStencilDesc.SampleDesc.Count = 1;
		depthStencilDesc.SampleDesc.Quality = 0;
		depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
		depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		depthStencilDesc.CPUAccessFlags = 0;
		depthStencilDesc.MiscFlags = 0;

		//View
		D3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc{};
		depthStencilViewDesc.Format = depthStencilDesc.Format;
		depthStencilViewDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		depthStencilViewDesc.Texture2D.MipSlice = 0;

		result = m_pDevice->CreateTexture2D(&depthStencilDesc, nullptr, &m_pDepthStencilBuffer);
		if (FAILED(result))
		{
			return result;
		}

		result = m_pDevice->CreateDepthStencilView(m_pDepthStencilBuffer, &depthStencilViewDesc, &m_pDepthStencilView);
		if (FAILED(result))
		{
			return result;
		}

		///4. Create RenderTarget (RT) and RenderTargetView (RTV)
		result = m_pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&m_pRenderTargetBuffer));
		if (FAILED(result))
		{
			return result;
		}

		//View
		result = m_pDevice->CreateRenderTargetView(m_pRenderTargetBuffer, nullptr, &m_pRenderTargetView);
		if (FAILED(result))
		{
			return result;
		}

		///5. Bind RTV and DSV to Output Merger Stage
		m_pDeviceContext->OMSetRenderTargets(1, &m_pRenderTargetView, m_pDepthStencilView);

		///6. Set viewport
		D3D11_VIEWPORT viewport{};
		viewport.Width = static_cast<FLOAT>(m_Width);
		viewport.Height = static_cast<FLOAT>(m_Height);
		viewport.TopLeftX = 0;
		viewport.TopLeftY = 0;
		viewport.MinDepth = 0;
		viewport.MaxDepth = 1;
		m_pDeviceContext->RSSetViewports(1, &viewport);

		return S_OK;
	}

	void Renderer::PressFilterMethod()
	{
		m_FilteringMethod = static_cast<FilteringMethods>((static_cast<int>(m_FilteringMethod) + 1) % 3);

		switch (m_FilteringMethod)
		{
		case dae::Point:
			std::cout << "POINT\n";
			m_SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
			break;
		case dae::Linear:
			std::cout << "LINEAR\n";
			m_SamplerDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			break;
		case dae::Anisotropic:
			std::cout << "ANISOTROPIC\n";
			m_SamplerDesc.Filter = D3D11_FILTER_ANISOTROPIC;
			break;
		}

		if (m_pSamplerState) m_pSamplerState->Release();

		HRESULT result{ m_pDevice->CreateSamplerState(&m_SamplerDesc, &m_pSamplerState) };

		if (FAILED(result)) return;

		m_pEffectSamplerVariable->SetSampler(0, m_pSamplerState);
	}

	void Renderer::CreateMesh()
	{
		std::vector<Vertex> vertices{};
		std::vector<uint32_t> indices{};

		Utils::ParseOBJ("Resources/vehicle.obj", vertices, indices);

		m_pMesh = new Mesh{ m_pDevice, vertices, indices };
	}
}
