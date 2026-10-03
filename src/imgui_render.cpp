// gtacheck — ImGui draw-data renderer for librw (D3D9).
//
// A copy of skeleton/imgui/imgui_impl_rw.cpp's RenderDrawLists with one
// difference: librw's im2d path silently drops any call with more than
// 10 000 vertices or indices (src/d3d/d3dimmed.cpp NUMVERTICES/NUMINDICES),
// and the stock backend hands every ImDrawCmd the whole draw list's vertex
// buffer.  A table with a few long text rows exceeds that and the whole
// window vanishes.  Here every command is rendered in chunks that fit, with
// the vertices referenced by each chunk compacted into a small buffer.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <rw.h>
#ifdef RW_D3D9
#include <d3d9.h>
namespace rw { namespace d3d { extern IDirect3DDevice9 *d3ddevice; } }
#endif
#include <skeleton.h>
#include "imgui/imgui_internal.h"
#include <vector>

using namespace rw::RWDEVICE;

static const int CHUNK_INDICES = 9999;	// multiple of 3, below librw's 10 000

// one window's draw list can be left out (or be the only one drawn): the glass panes are rendered in a
// second pass, over a texture that holds everything else
const char *GtaCheck_SkipOwner = nullptr;
const char *GtaCheck_OnlyOwner = nullptr;

ImDrawCallback GtaCheck_GlassBegin = nullptr, GtaCheck_GlassBeginData = nullptr, GtaCheck_GlassEnd = nullptr;
static rw::Texture *glassBackground;
static bool frameGlassOwner(const char *owner)
{
	if(strcmp(owner, "##edrail") == 0 || strcmp(owner, "##edstatus") == 0 || strcmp(owner, "##MainMenuBar") == 0) return true;
	// A dropdown attached to the caption is part of its glass surface.
	// Keep it out of the background pass so the caption cannot refract its edge again.
	ImGuiWindow *menu = ImGui::FindWindowByName(owner);
	ImGuiWindow *bar = ImGui::FindWindowByName("##MainMenuBar");
	return menu && bar && (menu->Flags & ImGuiWindowFlags_ChildMenu) &&
		menu->ParentWindow == bar;
}
static bool snapshotGlass()
{
	IDirect3DSurface9 *source = nullptr, *dest = nullptr;
	auto device = rw::d3d::d3ddevice;
	if(FAILED(device->GetRenderTarget(0, &source))) return false;
	D3DSURFACE_DESC desc;
	source->GetDesc(&desc);
	if(glassBackground && (glassBackground->raster->width != (int)desc.Width || glassBackground->raster->height != (int)desc.Height)){
		rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
		glassBackground->destroy(); glassBackground = nullptr;
	}
	if(!glassBackground){
		auto raster = rw::Raster::create(desc.Width, desc.Height, 0, rw::Raster::CAMERATEXTURE);
		if(raster) glassBackground = rw::Texture::create(raster);
	}
	bool ok = false;
	if(glassBackground){
		auto tex = (IDirect3DTexture9*)GETD3DRASTEREXT(glassBackground->raster)->texture;
		if(tex && SUCCEEDED(tex->GetSurfaceLevel(0, &dest))){
			ok = SUCCEEDED(device->StretchRect(source, nullptr, dest, nullptr, D3DTEXF_NONE));
			dest->Release();
		}
	}
	source->Release();
	return ok;
}

static std::vector<Im2DVertex> gVerts;		// compacted vertices for one chunk
static std::vector<uint16_t> gIdx;		// remapped indices for one chunk
static std::vector<int> gRemap;			// draw-list vertex index → chunk vertex index (-1 = unused)

void
GtaCheck_RenderDrawData(ImDrawData *draw_data)
{
	ImGuiIO &io = ImGui::GetIO();
	if(io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
		return;

	float xoff = 0.0f, yoff = 0.0f;
#ifdef RWHALFPIXEL
	xoff = -0.5f; yoff = 0.5f;
#endif
	rw::Camera *cam = (rw::Camera*)rw::engine->currentCamera;
	float recipZ = 1.0f / cam->nearPlane;

	ImVec2 clip_off = draw_data->DisplayPos;
	ImVec2 clip_scale = draw_data->FramebufferScale;

	int vertexAlpha = rw::GetRenderState(rw::VERTEXALPHA);
	int srcBlend = rw::GetRenderState(rw::SRCBLEND);
	int dstBlend = rw::GetRenderState(rw::DESTBLEND);
	int ztest = rw::GetRenderState(rw::ZTESTENABLE);
	void *tex0 = rw::GetRenderStatePtr(rw::TEXTURERASTER);
	int addrU = rw::GetRenderState(rw::TEXTUREADDRESSU);
	int addrV = rw::GetRenderState(rw::TEXTUREADDRESSV);
	int filter = rw::GetRenderState(rw::TEXTUREFILTER);
	int cullmode = rw::GetRenderState(rw::CULLMODE);
#ifdef RW_D3D9
	DWORD scissorEnabled = FALSE;
	RECT scissorRect;
	rw::d3d::d3ddevice->GetRenderState(D3DRS_SCISSORTESTENABLE, &scissorEnabled);
	rw::d3d::d3ddevice->GetScissorRect(&scissorRect);
#endif

	rw::SetRenderState(rw::VERTEXALPHA, 1);
	rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
	rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
	rw::SetRenderState(rw::ZTESTENABLE, 0);
	rw::SetRenderState(rw::CULLMODE, rw::CULLNONE);

	bool frameSnapshotReady = false;
	for(int n = 0; n < draw_data->CmdListsCount; n++){
		const ImDrawList *cmd_list = draw_data->CmdLists[n];
		if(GtaCheck_SkipOwner || GtaCheck_OnlyOwner){	// the glass pass draws the UI and the pane separately
			const char *owner = cmd_list->_OwnerName ? cmd_list->_OwnerName : "";
			auto matches = [&](const char *filter){
				if(strcmp(filter, "##glassframe") == 0)
					return frameGlassOwner(owner);
				return strcmp(owner, filter) == 0;
			};
			if(GtaCheck_SkipOwner && matches(GtaCheck_SkipOwner)) continue;
			if(GtaCheck_OnlyOwner && !matches(GtaCheck_OnlyOwner)) continue;
		}
		const ImDrawVert *vtx = cmd_list->VtxBuffer.Data;
		const ImDrawIdx *idx = cmd_list->IdxBuffer.Data;
		int numVtx = cmd_list->VtxBuffer.Size;
		if((int)gRemap.size() < numVtx) gRemap.resize((size_t)numVtx);

		const char *frameOwner = cmd_list->_OwnerName ? cmd_list->_OwnerName : "";
		bool framePane = frameGlassOwner(frameOwner);
		bool glass = false;
		for(int i = 0; i < cmd_list->CmdBuffer.Size; i++){
			const ImDrawCmd *pcmd = &cmd_list->CmdBuffer[i];
			if(pcmd->UserCallback){
				bool beginGlass = pcmd->UserCallback == GtaCheck_GlassBegin || pcmd->UserCallback == GtaCheck_GlassBeginData;
				if(beginGlass){
					// All frame pieces sample the same image, before any frame piece.
					// Recapturing between pieces refracts and tints the neighbouring bar twice.
					glass = framePane && frameSnapshotReady ? true : snapshotGlass();
					frameSnapshotReady = framePane && glass;
				}
				if(pcmd->UserCallback == GtaCheck_GlassEnd) glass = false;
				if(pcmd->UserCallback != ImDrawCallback_ResetRenderState)
					pcmd->UserCallback(cmd_list, pcmd);
				if(beginGlass && glass){
					float sourceRect[4] = {0, 0, io.DisplaySize.x, io.DisplaySize.y};
					rw::d3d::d3ddevice->SetPixelShaderConstantF(1, sourceRect, 1);
				}
				continue;
			}
			ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
			ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
			if(clip_max.x <= clip_min.x || clip_max.y <= clip_min.y)
				continue;
#ifdef RW_D3D9
			RECT r = { (LONG)clip_min.x, (LONG)clip_min.y, (LONG)clip_max.x, (LONG)clip_max.y };
			rw::d3d::d3ddevice->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
			rw::d3d::d3ddevice->SetScissorRect(&r);
#endif
			rw::Texture *tex = glass ? glassBackground : (rw::Texture*)pcmd->GetTexID();
			if(tex && tex->raster){
				rw::SetRenderStatePtr(rw::TEXTURERASTER, tex->raster);
				rw::SetRenderState(rw::TEXTUREADDRESSU, tex->getAddressU());
				rw::SetRenderState(rw::TEXTUREADDRESSV, tex->getAddressV());
				rw::SetRenderState(rw::TEXTUREFILTER, tex->getFilter());
			}else
				rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);

			// render the command's index range in chunks, compacting the vertices each chunk touches
			const ImDrawIdx *cmdIdx = idx + pcmd->IdxOffset;
			unsigned total = pcmd->ElemCount;
			for(unsigned start = 0; start < total; start += CHUNK_INDICES){
				unsigned count = total - start;
				if(count > (unsigned)CHUNK_INDICES) count = CHUNK_INDICES;
				gVerts.clear();
				gIdx.resize(count);
				// lazy remap: mark touched vertices
				for(unsigned k = 0; k < count; k++){
					int vi = (int)cmdIdx[start + k] + (int)pcmd->VtxOffset;
					if(vi < 0 || vi >= numVtx){ gIdx[k] = 0; continue; }
					gRemap[(size_t)vi] = -1;
				}
				for(unsigned k = 0; k < count; k++){
					int vi = (int)cmdIdx[start + k] + (int)pcmd->VtxOffset;
					if(vi < 0 || vi >= numVtx) continue;
					if(gRemap[(size_t)vi] < 0){
						gRemap[(size_t)vi] = (int)gVerts.size();
						const ImDrawVert &s = vtx[vi];
						Im2DVertex d;
						d.setScreenX(s.pos.x + xoff);
						d.setScreenY(s.pos.y + yoff);
						d.setScreenZ(rw::im2d::GetNearZ());
						d.setCameraZ(cam->nearPlane);
						d.setRecipCameraZ(recipZ);
						d.setColor(s.col & 0xFF, (s.col >> 8) & 0xFF, (s.col >> 16) & 0xFF, (s.col >> 24) & 0xFF);
						d.setU(glass ? s.pos.x / io.DisplaySize.x : s.uv.x, recipZ);
						d.setV(glass ? s.pos.y / io.DisplaySize.y : s.uv.y, recipZ);
						gVerts.push_back(d);
					}
					gIdx[k] = (uint16_t)gRemap[(size_t)vi];
				}
				if(!gVerts.empty())
					rw::im2d::RenderIndexedPrimitive(rw::PRIMTYPETRILIST, gVerts.data(), (rw::int32)gVerts.size(), gIdx.data(), (rw::int32)count);
			}
		}
	}

#ifdef RW_D3D9
	rw::d3d::d3ddevice->SetScissorRect(&scissorRect);
	rw::d3d::d3ddevice->SetRenderState(D3DRS_SCISSORTESTENABLE, scissorEnabled);
#endif
	rw::SetRenderState(rw::VERTEXALPHA, vertexAlpha);
	rw::SetRenderState(rw::SRCBLEND, srcBlend);
	rw::SetRenderState(rw::DESTBLEND, dstBlend);
	rw::SetRenderState(rw::ZTESTENABLE, ztest);
	// not tex0: the raster that was bound before may belong to a model the 3D map has since unloaded —
	// re-binding a freed raster hands D3D a dead texture. Unbind instead; every scene pass rebinds its own.
	(void)tex0;
	rw::SetRenderStatePtr(rw::TEXTURERASTER, nil);
	rw::SetRenderState(rw::TEXTUREADDRESSU, addrU);
	rw::SetRenderState(rw::TEXTUREADDRESSV, addrV);
	rw::SetRenderState(rw::TEXTUREFILTER, filter);
	rw::SetRenderState(rw::CULLMODE, cullmode);
}
