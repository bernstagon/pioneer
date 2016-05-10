// Copyright © 2008-2016 Pioneer Developers. See AUTHORS.txt for details
// Licensed under the terms of the GPL v3. See licenses/GPL-3.txt

#include "Sfx.h"
#include "Frame.h"
#include "galaxy/StarSystem.h"
#include "libs.h"
#include "Pi.h"
#include "Ship.h"
#include "Space.h"
#include "StringF.h"
#include "graphics/Drawables.h"
#include "graphics/Graphics.h"
#include "graphics/Material.h"
#include "graphics/Renderer.h"
#include "graphics/TextureBuilder.h"
#include "json/JsonUtils.h"

using namespace Graphics;

std::unique_ptr<Graphics::Material> SfxManager::damageParticle;
std::unique_ptr<Graphics::Material> SfxManager::ecmParticle;
std::unique_ptr<Graphics::Material> SfxManager::smokeParticle;
std::unique_ptr<Graphics::Material> SfxManager::explosionParticle;
Graphics::RenderState *SfxManager::alphaState = nullptr;
Graphics::RenderState *SfxManager::additiveAlphaState = nullptr;
Graphics::RenderState *SfxManager::alphaOneState = nullptr;

Graphics::Texture* SfxManager::explosionTextures[NUM_EXPLOSION_TEXTURES];

Sfx::Sfx() : m_speed(200.0f), m_type(TYPE_NONE)
{
}

Sfx::Sfx(vector3d &pos, vector3d &vel, float speed, SFX_TYPE type) :
	m_pos(pos),	m_vel(vel),	m_age(0.0f), m_speed(speed), m_type(type)
{
}

void Sfx::SaveToJson(Json::Value &jsonObj)
{
	Json::Value sfxObj(Json::objectValue); // Create JSON object to contain sfx data.

	VectorToJson(sfxObj, m_pos, "pos");
	VectorToJson(sfxObj, m_vel, "vel");
	sfxObj["age"] = FloatToStr(m_age);
	sfxObj["type"] = m_type;

	jsonObj["sfx"] = sfxObj; // Add sfx object to supplied object.
}

void Sfx::LoadFromJson(const Json::Value &jsonObj)
{
	if (!jsonObj.isMember("sfx")) throw SavedGameCorruptException();
	Json::Value sfxObj = jsonObj["sfx"];
	if (!sfxObj.isMember("pos")) throw SavedGameCorruptException();
	if (!sfxObj.isMember("vel")) throw SavedGameCorruptException();
	if (!sfxObj.isMember("age")) throw SavedGameCorruptException();
	if (!sfxObj.isMember("type")) throw SavedGameCorruptException();

	JsonToVector(&m_pos, sfxObj, "pos");
	JsonToVector(&m_vel, sfxObj, "vel");
	m_age = StrToFloat(sfxObj["age"].asString());
	m_type = static_cast<SFX_TYPE>(sfxObj["type"].asInt());
}

void Sfx::SetPosition(const vector3d &p)
{
	m_pos = p;
}

void Sfx::TimeStepUpdate(const float timeStep)
{
	PROFILE_SCOPED()
	m_age += timeStep;
	m_pos += m_vel * double(timeStep);

	switch (m_type) {
		case TYPE_EXPLOSION:	if (m_age > 3.2) m_type = TYPE_NONE;	break;
		case TYPE_DAMAGE:		if (m_age > 2.0) m_type = TYPE_NONE;	break;
		case TYPE_SMOKE:		if (m_age > 8.0) m_type = TYPE_NONE;	break;
		case TYPE_NONE: break;
	}
}

float SizeToPixels(const matrix4x4d &trans, const float size)
{
	//some hand-tweaked scaling, to make the lights seem larger from distance (final size is in pixels)
	// gl_PointSize = pixels_per_radian * point_diameter / distance( camera, pointcenter );
	const float pixrad = Clamp(Graphics::GetScreenHeight() / trans.GetTranslate().Length(), 0.1, 50.0);
	return (size * Graphics::GetFovFactor()) * pixrad;
}

void Sfx::Render(Renderer *renderer, const matrix4x4d &ftransform)
{
	PROFILE_SCOPED()
	const vector3d fpos = ftransform * GetPosition();
	const vector3f pos(fpos);
	const matrix4x4d fposTrans(matrix4x4d::Translation(fpos));

	switch (m_type) 
	{
		case TYPE_NONE: break;
		case TYPE_EXPLOSION: 
		{
			renderer->SetTransform(fposTrans);
			const int spriteframe = Clamp( Uint32(m_age*20.0f), Uint32(0), NUM_EXPLOSION_TEXTURES-1 );
			assert(SfxManager::explosionTextures[spriteframe]);
			SfxManager::explosionParticle->texture0 = SfxManager::explosionTextures[spriteframe];
			//face camera
			renderer->SetTransform(matrix4x4f::Identity());
			renderer->DrawPointSprites(1, &pos, SfxManager::alphaState, SfxManager::explosionParticle.get(), SizeToPixels(fposTrans, m_speed));
			break;
		} 
		case TYPE_DAMAGE: 
		{
			renderer->SetTransform(fposTrans);
			SfxManager::damageParticle->diffuse = Color(255, 255, 0, (1.0f-(m_age/2.0f))*255);
			renderer->DrawPointSprites(1, &pos, SfxManager::additiveAlphaState, SfxManager::damageParticle.get(), SizeToPixels(fposTrans, 20.f));
			break;
		} 
		case TYPE_SMOKE: 
		{
			float var = Pi::rng.Double()*0.05f; //slightly variation to trail color
			if (m_age < 0.5) { //start trail
				SfxManager::smokeParticle->diffuse = Color((0.75f-var)*255, (0.75f-var)*255, (0.75f-var)*255, (m_age*0.5-(m_age/2.0f))*255);
			} else { //end trail
				SfxManager::smokeParticle->diffuse = Color((0.75-var)*255, (0.75f-var)*255, (0.75f-var)*255, Clamp(0.5*0.5-(m_age/16.0),0.0,1.0)*255);
			}

			renderer->SetTransform(fposTrans);

			SfxManager::smokeParticle->diffuse*=0.05;
			renderer->DrawPointSprites(1, &pos, SfxManager::alphaState, SfxManager::smokeParticle.get(), Clamp(SizeToPixels(fposTrans, (m_speed*m_age)), 0.1f, 50.0f));
			break;
		}
	}
}

void SfxManager::ToJson(Json::Value &jsonObj, const Frame *f)
{
	Json::Value sfxArray(Json::arrayValue); // Create JSON array to contain sfx data.

	if (f->m_sfx)
	{
		for(size_t t=0; t<TYPE_NONE; t++) 
		{
			for (size_t i = 0; i < f->m_sfx->GetNumberInstances(SFX_TYPE(t)); i++)
			{
				Sfx &inst(f->m_sfx->GetInstanceByIndex(SFX_TYPE(t), i));
				if (inst.m_type != TYPE_NONE)
				{
					Json::Value sfxArrayEl(Json::objectValue); // Create JSON object to contain sfx element.
					inst.SaveToJson(sfxArrayEl);
					sfxArray.append(sfxArrayEl); // Append sfx object to array.
				}
			}
		}
	}

	jsonObj["sfx_array"] = sfxArray; // Add sfx array to supplied object.
}

void SfxManager::FromJson(const Json::Value &jsonObj, Frame *f)
{
	if (!jsonObj.isMember("sfx_array")) throw SavedGameCorruptException();
	Json::Value sfxArray = jsonObj["sfx_array"];
	if (!sfxArray.isArray()) throw SavedGameCorruptException();

	if (sfxArray.size()) f->m_sfx.reset(new SfxManager);
	for (unsigned int i = 0; i < sfxArray.size(); ++i)
	{
		Sfx inst; inst.LoadFromJson(sfxArray[i]);
		f->m_sfx->AddInstance(inst);
	}
}

SfxManager *SfxManager::AllocSfxInFrame(Frame *f)
{
	if (!f->m_sfx) {
		f->m_sfx.reset(new SfxManager);
	}

	return f->m_sfx.get();
}

void SfxManager::Add(const Body *b, SFX_TYPE t)
{
	SfxManager *sfxman = AllocSfxInFrame(b->GetFrame());
	if (!sfxman) return;
	vector3d vel(b->GetVelocity() + 200.0*vector3d(Pi::rng.Double()-0.5,Pi::rng.Double()-0.5,Pi::rng.Double()-0.5));
	Sfx sfx(b->GetPosition(), vel, 200, t);
	sfxman->AddInstance(sfx);
}

void SfxManager::AddExplosion(Body *b)
{
	SfxManager *sfxman = AllocSfxInFrame(b->GetFrame());
	if (!sfxman) return;
	
	float speed = 200.0f;
	if (b->IsType(Object::SHIP)) {
		Ship *s = static_cast<Ship*>(b);
		speed = s->GetAabb().radius*8.0;
	}
	Sfx sfx(b->GetPosition(), b->GetVelocity(), speed, TYPE_EXPLOSION);
	sfxman->AddInstance(sfx);
}

void SfxManager::AddThrustSmoke(const Body *b, const float speed, const vector3d &adjustpos)
{
	SfxManager *sfxman = AllocSfxInFrame(b->GetFrame());
	if (!sfxman) return;

	Sfx sfx(b->GetPosition()+adjustpos, vector3d(0,0,0), speed, TYPE_SMOKE);
	sfxman->AddInstance(sfx);
}

void SfxManager::TimeStepAll(const float timeStep, Frame *f)
{
	PROFILE_SCOPED()
	if (f->m_sfx) {
		for(size_t t=0; t<TYPE_NONE; t++) 
		{
			for (size_t i = 0; i < f->m_sfx->GetNumberInstances(SFX_TYPE(t)); i++)
			{
				Sfx &inst(f->m_sfx->GetInstanceByIndex(SFX_TYPE(t), i));
				inst.TimeStepUpdate(timeStep);
			}
		}
	}

	for (Frame* kid : f->GetChildren()) {
		TimeStepAll(timeStep, kid);
	}
}

void SfxManager::Cleanup()
{
	for(size_t t=0; t<TYPE_NONE; t++) 
	{
		for (size_t i = 0; i < GetNumberInstances(SFX_TYPE(t)); i++)
		{
			Sfx &inst(GetInstanceByIndex(SFX_TYPE(t), i));
			if (inst.m_type == TYPE_NONE)
			{
				m_instances[t].erase(m_instances[t].begin()+i);
			}
		}
	}
}

void SfxManager::RenderAll(Renderer *renderer, Frame *f, const Frame *camFrame)
{
	PROFILE_SCOPED()
	if (f->m_sfx) {
		matrix4x4d ftran;
		Frame::GetFrameTransform(f, camFrame, ftran);

		for(size_t t=0; t<TYPE_NONE; t++) 
		{
			for (size_t i = 0; i < f->m_sfx->GetNumberInstances(SFX_TYPE(t)); i++)
			{
				Sfx &inst(f->m_sfx->GetInstanceByIndex(SFX_TYPE(t), i));
				inst.Render(renderer, ftran);
			}
		}
	}

	for (Frame* kid : f->GetChildren()) {
		RenderAll(renderer, kid, camFrame);
	}
}

void SfxManager::Init(Graphics::Renderer *r)
{
	//shared render states
	Graphics::RenderStateDesc rsd;
	rsd.blendMode = Graphics::BLEND_ALPHA;
	rsd.depthWrite = false;
	alphaState = r->CreateRenderState(rsd);

	rsd.blendMode = Graphics::BLEND_ALPHA_ONE;
	additiveAlphaState = r->CreateRenderState(rsd);

	rsd.depthWrite = true;
	alphaOneState = r->CreateRenderState(rsd);

	Graphics::MaterialDescriptor desc;
	desc.effect = Graphics::EFFECT_BILLBOARD;
	desc.textures = 1;
	RefCountedPtr<Graphics::Material> explosionMat(r->CreateMaterial(desc));

	damageParticle.reset( r->CreateMaterial(desc) );
	damageParticle->texture0 = Graphics::TextureBuilder::Billboard("textures/smoke.png").GetOrCreateTexture(r, "billboard");
	ecmParticle.reset( r->CreateMaterial(desc) );
	ecmParticle->texture0 = Graphics::TextureBuilder::Billboard("textures/ecm.png").GetOrCreateTexture(r, "billboard");
	smokeParticle.reset( r->CreateMaterial(desc) );
	smokeParticle->texture0 = Graphics::TextureBuilder::Billboard("textures/smoke.png").GetOrCreateTexture(r, "billboard");
	explosionParticle.reset( r->CreateMaterial(desc) );
	explosionParticle->texture0 = Graphics::TextureBuilder::Billboard("textures/smoke.png").GetOrCreateTexture(r, "billboard");

	// NB: 0-31
	for( Uint32 i=0 ; i<NUM_EXPLOSION_TEXTURES ; i++ )
	{
		const std::string fname(stringf("textures/explosions/image%0.png", i));
		explosionTextures[i] = Graphics::TextureBuilder::Billboard(fname).GetOrCreateTexture(r, "billboard");
	}
}

void SfxManager::Uninit()
{
	damageParticle.reset();
	ecmParticle.reset();
	smokeParticle.reset();
	explosionParticle.reset();
}
