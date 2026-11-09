#include <pch.h>
#include "particle_effect.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace wiwa
{
	// ------------------------------------------------------------------------------------------------------------- values

	ParticleValue ParticleValue::constant(float value)
	{
		ParticleValue result;
		result.Kind = Mode::Constant;
		result.A = value;
		return result;
	}

	ParticleValue ParticleValue::range(float a, float b)
	{
		ParticleValue result;
		result.Kind = Mode::Range;
		result.A = a;
		result.B = b;
		return result;
	}

	ParticleValue ParticleValue::curve(std::vector<ParticleCurveKey> keys)
	{
		ParticleValue result;
		result.Kind = Mode::Curve;
		result.Keys = std::move(keys);
		result.bake();
		return result;
	}

	void ParticleValue::bake()
	{
		if (Kind != Mode::Curve)
		{
			return;
		}

		Samples.fill(0.0f);
		if (Keys.empty())
		{
			return;
		}
		for (u32 i = 0; i < PARTICLE_CURVE_SAMPLES; i++)
		{
			const float t = static_cast<float>(i) / static_cast<float>(PARTICLE_CURVE_SAMPLES - 1);
			if (t <= Keys.front().Time)
			{
				Samples[i] = Keys.front().Value;
			}
			else if (t >= Keys.back().Time)
			{
				Samples[i] = Keys.back().Value;
			}
			else
			{
				size_t next = 1;
				while (next < Keys.size() && Keys[next].Time < t)
				{
					next++;
				}
				const ParticleCurveKey& a = Keys[next - 1];
				const ParticleCurveKey& b = Keys[next];
				const float span = b.Time - a.Time;
				Samples[i] = span > 0.0f ? a.Value + (b.Value - a.Value) * ((t - a.Time) / span) : b.Value;
			}
		}
	}

	float ParticleValue::evaluate(float t) const
	{
		switch (Kind)
		{
		case Mode::Constant:
			return A;
		case Mode::Range:
			return A + (B - A) * t;
		case Mode::Curve:
		default:
		{
			const float position = std::clamp(t, 0.0f, 1.0f) * static_cast<float>(PARTICLE_CURVE_SAMPLES - 1);
			const u32 low = std::min(static_cast<u32>(position), PARTICLE_CURVE_SAMPLES - 1);
			const u32 high = std::min(low + 1, PARTICLE_CURVE_SAMPLES - 1);
			return Samples[low] + (Samples[high] - Samples[low]) * (position - static_cast<float>(low));
		}
		}
	}

	void ParticleEmitterData::bake()
	{
		for (ParticleValue* value : { &Rate, &Lifetime, &StartSpeed, &StartSize, &StartRotation, &SizeOverLife, &RotationSpeed, &AlphaOverLife, &FrameOverLife })
		{
			value->bake();
		}

		ColorSamples.fill(glm::vec4(1.0f));
		if (ColorOverLife.empty())
		{
			return;
		}
		for (u32 i = 0; i < PARTICLE_CURVE_SAMPLES; i++)
		{
			const float t = static_cast<float>(i) / static_cast<float>(PARTICLE_CURVE_SAMPLES - 1);
			if (t <= ColorOverLife.front().Time)
			{
				ColorSamples[i] = ColorOverLife.front().Color;
			}
			else if (t >= ColorOverLife.back().Time)
			{
				ColorSamples[i] = ColorOverLife.back().Color;
			}
			else
			{
				size_t next = 1;
				while (next < ColorOverLife.size() && ColorOverLife[next].Time < t)
				{
					next++;
				}
				const ParticleColorKey& a = ColorOverLife[next - 1];
				const ParticleColorKey& b = ColorOverLife[next];
				const float span = b.Time - a.Time;
				ColorSamples[i] = span > 0.0f ? glm::mix(a.Color, b.Color, (t - a.Time) / span) : b.Color;
			}
		}
	}

	// ------------------------------------------------------------------------------------------------------------- JSON

	namespace
	{
		using json = nlohmann::json;

		// The shortest decimal text that reads back as the same float, so that files stay readable (0.1, not 0.10000000149011612).
		double short_float(float value)
		{
			char text[32];
			for (int precision = 1; precision <= 9; precision++)
			{
				std::snprintf(text, sizeof(text), "%.*g", precision, static_cast<double>(value));
				if (std::strtof(text, nullptr) == value)
				{
					break;
				}
			}
			return std::strtod(text, nullptr);
		}

		json floats(const float* values, size_t count)
		{
			json result = json::array();
			for (size_t i = 0; i < count; i++)
			{
				result.push_back(short_float(values[i]));
			}
			return result;
		}

		template<size_t N>
		const char* name_of(const std::array<const char*, N>& names, size_t index)
		{
			return index < N ? names[index] : names[0];
		}

		constexpr std::array<const char*, 3> RENDERER_NAMES{ "sprite", "ribbon", "mesh" };
		constexpr std::array<const char*, 3> BLEND_NAMES{ "additive", "alpha", "multiply" };
		constexpr std::array<const char*, 4> ALIGN_NAMES{ "camera", "velocity", "axis", "world" };
		constexpr std::array<const char*, 2> COLLISION_NAMES{ "none", "depth" };
		constexpr std::array<const char*, 3> RESPONSE_NAMES{ "bounce", "stick", "kill" };
		constexpr std::array<const char*, 4> SHAPE_NAMES{ "point", "sphere", "cone", "box" };

		json ref_json(const AssetRef& ref)
		{
			if (!ref.valid())
			{
				return nullptr;
			}
			json result = json::object();
			result["guid"] = ref.Guid.to_string();
			if (ref.LocalId != 0)
			{
				result["local"] = ref.LocalId;
			}
			return result;
		}

		json value_json(const ParticleValue& value)
		{
			switch (value.Kind)
			{
			case ParticleValue::Mode::Range:
				return json::array({ short_float(value.A), short_float(value.B) });
			case ParticleValue::Mode::Curve:
			{
				json keys = json::array();
				for (const ParticleCurveKey& key : value.Keys)
				{
					keys.push_back(json::array({ short_float(key.Time), short_float(key.Value) }));
				}
				json result = json::object();
				result["curve"] = keys;
				return result;
			}
			case ParticleValue::Mode::Constant:
			default:
				return short_float(value.A);
			}
		}

		// Reads one field of one object, remembering the first problem. A key that is absent leaves the value as it is.
		class Reader
		{
		public:
			explicit Reader(std::string& error) : m_Error(error) {}
			bool ok() const { return m_Error.empty(); }

			void fail(const std::string& message)
			{
				if (m_Error.empty())
				{
					m_Error = message;
				}
			}

			const json* find(const json& object, const char* key)
			{
				const auto it = object.find(key);
				return it == object.end() ? nullptr : &*it;
			}

			void number(const json& object, const char* key, float& out)
			{
				if (const json* v = find(object, key))
				{
					if (!v->is_number() || !std::isfinite(v->get<float>()))
					{
						fail(std::string("\"") + key + "\" is not a finite number");
						return;
					}
					out = v->get<float>();
				}
			}

			void count(const json& object, const char* key, u32& out)
			{
				if (const json* v = find(object, key))
				{
					if (!v->is_number_unsigned())
					{
						fail(std::string("\"") + key + "\" is not a whole number of zero or more");
						return;
					}
					out = v->get<u32>();
				}
			}

			void boolean(const json& object, const char* key, bool& out)
			{
				if (const json* v = find(object, key))
				{
					if (!v->is_boolean())
					{
						fail(std::string("\"") + key + "\" is not true or false");
						return;
					}
					out = v->get<bool>();
				}
			}

			template<typename E, size_t N>
			void choice(const json& object, const char* key, const std::array<const char*, N>& names, E& out)
			{
				if (const json* v = find(object, key))
				{
					if (v->is_string())
					{
						for (size_t i = 0; i < N; i++)
						{
							if (v->get<std::string>() == names[i])
							{
								out = static_cast<E>(i);
								return;
							}
						}
					}
					std::string options;
					for (const char* name : names)
					{
						options += (options.empty() ? "" : ", ") + std::string(name);
					}
					fail(std::string("\"") + key + "\" is not one of " + options);
				}
			}

			template<size_t N>
			void vector(const json& object, const char* key, float* out)
			{
				if (const json* v = find(object, key))
				{
					float read[N]{};
					if (!v->is_array() || v->size() != N)
					{
						fail(std::string("\"") + key + "\" is not a list of " + std::to_string(N) + " numbers");
						return;
					}
					for (size_t i = 0; i < N; i++)
					{
						if (!(*v)[i].is_number() || !std::isfinite((*v)[i].get<float>()))
						{
							fail(std::string("\"") + key + "\" holds a number that is not finite");
							return;
						}
						read[i] = (*v)[i].get<float>();
					}
					std::copy(read, read + N, out);
				}
			}

			void ref(const json& object, const char* key, AssetRef& out)
			{
				const json* v = find(object, key);
				if (v == nullptr)
				{
					return;
				}
				out = AssetRef{};
				if (v->is_null())
				{
					return;
				}
				if (!v->is_object() || !v->contains("guid") || !(*v)["guid"].is_string())
				{
					fail(std::string("\"") + key + "\" is not an asset reference");
					return;
				}
				const std::optional<AssetGuid> guid = AssetGuid::from_string((*v)["guid"].get<std::string>());
				if (!guid)
				{
					fail(std::string("\"") + key + "\" has a guid that is not one");
					return;
				}
				out.Guid = *guid;
				if (v->contains("local"))
				{
					if (!(*v)["local"].is_number_unsigned())
					{
						fail(std::string("\"") + key + "\" has a local id that is not a whole number");
						return;
					}
					out.LocalId = (*v)["local"].get<u64>();
				}
			}

			void value(const json& object, const char* key, ParticleValue& out)
			{
				const json* v = find(object, key);
				if (v == nullptr)
				{
					return;
				}
				const std::string what = std::string("\"") + key + "\"";
				if (v->is_number())
				{
					if (!std::isfinite(v->get<float>()))
					{
						fail(what + " is not finite");
						return;
					}
					out = ParticleValue::constant(v->get<float>());
				}
				else if (v->is_array())
				{
					if (v->size() != 2 || !(*v)[0].is_number() || !(*v)[1].is_number() || !std::isfinite((*v)[0].get<float>()) || !std::isfinite((*v)[1].get<float>()))
					{
						fail(what + " is a list but not a range of two finite numbers");
						return;
					}
					out = ParticleValue::range((*v)[0].get<float>(), (*v)[1].get<float>());
				}
				else if (v->is_object() && v->contains("curve") && (*v)["curve"].is_array())
				{
					std::vector<ParticleCurveKey> keys;
					for (const json& entry : (*v)["curve"])
					{
						if (!entry.is_array() || entry.size() != 2 || !entry[0].is_number() || !entry[1].is_number() || !std::isfinite(entry[0].get<float>()) || !std::isfinite(entry[1].get<float>()))
						{
							fail(what + " has a curve key that is not [time, value]");
							return;
						}
						keys.push_back(ParticleCurveKey{ entry[0].get<float>(), entry[1].get<float>() });
					}
					out = ParticleValue::curve(std::move(keys));
				}
				else
				{
					fail(what + " is not a number, a [min, max] range or a { \"curve\": [...] }");
				}
			}

		private:
			std::string& m_Error;
		};

		void write_emitter(json& out, const ParticleEmitterData& e)
		{
			out["name"] = e.Name;
			out["renderer"] = name_of(RENDERER_NAMES, static_cast<size_t>(e.Renderer));
			out["material"] = ref_json(e.Material);
			out["mesh"] = ref_json(e.Mesh);
			out["sheet"] = json::array({ e.SheetColumns, e.SheetRows });
			out["align"] = name_of(ALIGN_NAMES, static_cast<size_t>(e.Align));
			out["blend"] = name_of(BLEND_NAMES, static_cast<size_t>(e.Blend));
			out["softDistance"] = short_float(e.SoftDistance);
			out["intensity"] = short_float(e.Intensity);
			out["sorted"] = e.Sorted;
			out["lit"] = e.Lit;
			out["maxParticles"] = e.MaxParticles;
			json ribbon = json::object();
			ribbon["segments"] = e.RibbonSegments;
			ribbon["minSegmentLength"] = short_float(e.MinSegmentLength);
			out["ribbon"] = ribbon;
			json collision = json::object();
			collision["mode"] = name_of(COLLISION_NAMES, static_cast<size_t>(e.Collision));
			collision["response"] = name_of(RESPONSE_NAMES, static_cast<size_t>(e.Response));
			collision["restitution"] = short_float(e.Restitution);
			out["collision"] = collision;

			json spawn = json::object();
			spawn["looping"] = e.Looping;
			spawn["duration"] = short_float(e.Duration);
			spawn["delay"] = short_float(e.StartDelay);
			spawn["rate"] = value_json(e.Rate);
			json bursts = json::array();
			for (const ParticleBurst& burst : e.Bursts)
			{
				json b = json::object();
				b["time"] = short_float(burst.Time);
				b["count"] = burst.Count;
				bursts.push_back(b);
			}
			spawn["bursts"] = bursts;
			spawn["shape"] = name_of(SHAPE_NAMES, static_cast<size_t>(e.Shape));
			spawn["shapeSize"] = floats(&e.ShapeSize.x, 3);
			spawn["coneAngle"] = short_float(e.ConeAngle);
			spawn["worldSpace"] = e.WorldSpace;
			out["spawn"] = spawn;

			json start = json::object();
			start["lifetime"] = value_json(e.Lifetime);
			start["speed"] = value_json(e.StartSpeed);
			start["size"] = value_json(e.StartSize);
			start["rotation"] = value_json(e.StartRotation);
			start["color"] = floats(&e.StartColor.x, 4);
			out["start"] = start;

			json forces = json::object();
			forces["gravity"] = floats(&e.Gravity.x, 3);
			forces["drag"] = short_float(e.Drag);
			forces["noiseStrength"] = short_float(e.NoiseStrength);
			forces["noiseFrequency"] = short_float(e.NoiseFrequency);
			out["forces"] = forces;

			json life = json::object();
			life["size"] = value_json(e.SizeOverLife);
			life["rotationSpeed"] = value_json(e.RotationSpeed);
			life["alpha"] = value_json(e.AlphaOverLife);
			life["frame"] = value_json(e.FrameOverLife);
			json colors = json::array();
			for (const ParticleColorKey& key : e.ColorOverLife)
			{
				json c = json::object();
				c["time"] = short_float(key.Time);
				c["color"] = floats(&key.Color.x, 4);
				colors.push_back(c);
			}
			life["color"] = colors;
			out["overLife"] = life;
		}

		bool read_emitter(const json& in, ParticleEmitterData& e, std::string& error)
		{
			Reader r(error);
			if (!in.is_object())
			{
				error = "an emitter is not an object";
				return false;
			}

			if (const json* name = r.find(in, "name"))
			{
				if (!name->is_string())
				{
					r.fail("\"name\" is not text");
				}
				else
				{
					e.Name = name->get<std::string>();
				}
			}
			r.choice(in, "renderer", RENDERER_NAMES, e.Renderer);
			r.ref(in, "material", e.Material);
			r.ref(in, "mesh", e.Mesh);
			if (const json* sheet = r.find(in, "sheet"))
			{
				if (!sheet->is_array() || sheet->size() != 2 || !(*sheet)[0].is_number_unsigned() || !(*sheet)[1].is_number_unsigned())
				{
					r.fail("\"sheet\" is not [columns, rows]");
				}
				else
				{
					e.SheetColumns = (*sheet)[0].get<u32>();
					e.SheetRows = (*sheet)[1].get<u32>();
				}
			}
			r.choice(in, "align", ALIGN_NAMES, e.Align);
			r.choice(in, "blend", BLEND_NAMES, e.Blend);
			r.number(in, "softDistance", e.SoftDistance);
			r.number(in, "intensity", e.Intensity);
			r.boolean(in, "sorted", e.Sorted);
			r.boolean(in, "lit", e.Lit);
			r.count(in, "maxParticles", e.MaxParticles);
			if (const json* ribbon = r.find(in, "ribbon"); ribbon != nullptr && ribbon->is_object())
			{
				r.count(*ribbon, "segments", e.RibbonSegments);
				r.number(*ribbon, "minSegmentLength", e.MinSegmentLength);
			}
			if (const json* collision = r.find(in, "collision"); collision != nullptr && collision->is_object())
			{
				r.choice(*collision, "mode", COLLISION_NAMES, e.Collision);
				r.choice(*collision, "response", RESPONSE_NAMES, e.Response);
				r.number(*collision, "restitution", e.Restitution);
			}
			if (const json* spawn = r.find(in, "spawn"); spawn != nullptr && spawn->is_object())
			{
				r.boolean(*spawn, "looping", e.Looping);
				r.number(*spawn, "duration", e.Duration);
				r.number(*spawn, "delay", e.StartDelay);
				r.value(*spawn, "rate", e.Rate);
				if (const json* bursts = r.find(*spawn, "bursts"))
				{
					e.Bursts.clear();
					if (!bursts->is_array())
					{
						r.fail("\"bursts\" is not a list");
					}
					else
					{
						for (const json& entry : *bursts)
						{
							ParticleBurst burst;
							if (!entry.is_object())
							{
								r.fail("a burst is not an object");
								break;
							}
							r.number(entry, "time", burst.Time);
							r.count(entry, "count", burst.Count);
							e.Bursts.push_back(burst);
						}
					}
				}
				r.choice(*spawn, "shape", SHAPE_NAMES, e.Shape);
				r.vector<3>(*spawn, "shapeSize", &e.ShapeSize.x);
				r.number(*spawn, "coneAngle", e.ConeAngle);
				r.boolean(*spawn, "worldSpace", e.WorldSpace);
			}
			if (const json* start = r.find(in, "start"); start != nullptr && start->is_object())
			{
				r.value(*start, "lifetime", e.Lifetime);
				r.value(*start, "speed", e.StartSpeed);
				r.value(*start, "size", e.StartSize);
				r.value(*start, "rotation", e.StartRotation);
				r.vector<4>(*start, "color", &e.StartColor.x);
			}
			if (const json* forces = r.find(in, "forces"); forces != nullptr && forces->is_object())
			{
				r.vector<3>(*forces, "gravity", &e.Gravity.x);
				r.number(*forces, "drag", e.Drag);
				r.number(*forces, "noiseStrength", e.NoiseStrength);
				r.number(*forces, "noiseFrequency", e.NoiseFrequency);
			}
			if (const json* life = r.find(in, "overLife"); life != nullptr && life->is_object())
			{
				r.value(*life, "size", e.SizeOverLife);
				r.value(*life, "rotationSpeed", e.RotationSpeed);
				r.value(*life, "alpha", e.AlphaOverLife);
				r.value(*life, "frame", e.FrameOverLife);
				if (const json* colors = r.find(*life, "color"))
				{
					e.ColorOverLife.clear();
					if (!colors->is_array())
					{
						r.fail("\"color\" over the life is not a list");
					}
					else
					{
						for (const json& entry : *colors)
						{
							ParticleColorKey key;
							if (!entry.is_object())
							{
								r.fail("a color key is not an object");
								break;
							}
							r.number(entry, "time", key.Time);
							r.vector<4>(entry, "color", &key.Color.x);
							e.ColorOverLife.push_back(key);
						}
					}
				}
			}

			e.bake();
			return r.ok();
		}
	}

	std::string write_particle_file(const ParticleEffectData& effect)
	{
		json root = json::object();
		root["version"] = WFX_VERSION;
		root["name"] = effect.Name;
		json emitters = json::array();
		for (const ParticleEmitterData& emitter : effect.Emitters)
		{
			json e = json::object();
			write_emitter(e, emitter);
			emitters.push_back(e);
		}
		root["emitters"] = emitters;
		return root.dump(2) + "\n";
	}

	std::optional<ParticleEffectData> read_particle_file(std::string_view text, std::string& error)
	{
		error.clear();
		json root = json::parse(text.begin(), text.end(), nullptr, false);
		if (root.is_discarded() || !root.is_object())
		{
			error = "this is not a particle effect file (not JSON)";
			return std::nullopt;
		}
		if (!root.contains("version") || !root["version"].is_number_integer() || root["version"].get<int>() != WFX_VERSION)
		{
			error = "particle effect file version " + (root.contains("version") ? root["version"].dump() : std::string("(none)")) + " is not supported (this editor reads version " + std::to_string(WFX_VERSION) + ")";
			return std::nullopt;
		}

		ParticleEffectData effect;
		effect.Name = root.value("name", std::string{});
		if (root.contains("emitters"))
		{
			if (!root["emitters"].is_array())
			{
				error = "\"emitters\" is not a list";
				return std::nullopt;
			}
			// A file with too many emitters is read whole (the editor can show it); particle_effect_valid rejects it.
			for (const json& entry : root["emitters"])
			{
				ParticleEmitterData emitter;
				std::string problem;
				if (!read_emitter(entry, emitter, problem))
				{
					error = "emitter " + std::to_string(effect.Emitters.size()) + ": " + problem;
					return std::nullopt;
				}
				effect.Emitters.push_back(std::move(emitter));
			}
		}
		return effect;
	}

	// ------------------------------------------------------------------------------------------------------------- rules

	namespace
	{
		bool finite(float v) { return std::isfinite(v); }

		template<typename V>
		bool finite_vec(const V& v) { return !glm::any(glm::isnan(v)) && !glm::any(glm::isinf(v)); }

		bool value_valid(const ParticleValue& value, const char* what, bool positive, std::string& error)
		{
			const auto bad = [&](const std::string& why)
			{
				error = std::string(what) + ": " + why;
				return false;
			};

			switch (value.Kind)
			{
			case ParticleValue::Mode::Constant:
				if (!finite(value.A) || (positive && !(value.A > 0.0f)))
				{
					return bad(positive ? "must be above zero" : "must be a finite number");
				}
				return true;
			case ParticleValue::Mode::Range:
				if (!finite(value.A) || !finite(value.B) || value.B < value.A || (positive && !(value.A > 0.0f)))
				{
					return bad(positive ? "a range must run from above zero upwards" : "a range must run from the smaller number to the larger");
				}
				return true;
			case ParticleValue::Mode::Curve:
			default:
			{
				if (value.Keys.empty())
				{
					return bad("a curve needs at least one key");
				}
				float last = -1.0f;
				for (const ParticleCurveKey& key : value.Keys)
				{
					if (!finite(key.Time) || !finite(key.Value) || key.Time < 0.0f || key.Time > 1.0f || key.Time <= last)
					{
						return bad("curve keys need finite numbers and times that rise within 0 to 1");
					}
					if (positive && !(key.Value > 0.0f))
					{
						return bad("every key of this curve must be above zero");
					}
					last = key.Time;
				}
				return true;
			}
			}
		}
	}

	bool particle_effect_valid(const ParticleEffectData& effect, std::string& error)
	{
		error.clear();
		if (effect.Emitters.size() > PARTICLE_MAX_EMITTERS)
		{
			error = "an effect has at most " + std::to_string(PARTICLE_MAX_EMITTERS) + " emitters (this one has " + std::to_string(effect.Emitters.size()) + ")";
			return false;
		}

		std::unordered_set<std::string> names;
		for (const ParticleEmitterData& e : effect.Emitters)
		{
			const std::string who = "emitter \"" + e.Name + "\": ";
			const auto bad = [&](const std::string& why)
			{
				error = who + why;
				return false;
			};

			if (e.Name.empty() || !names.insert(e.Name).second)
			{
				return bad(e.Name.empty() ? "has no name" : "its name is used by another emitter");
			}
			if (e.MaxParticles < 1 || e.MaxParticles > PARTICLE_LIMIT_MAX_PARTICLES)
			{
				return bad("maxParticles must be 1 to " + std::to_string(PARTICLE_LIMIT_MAX_PARTICLES));
			}
			if (e.SheetColumns < 1 || e.SheetRows < 1 || e.SheetColumns * e.SheetRows > 256)
			{
				return bad("the sprite sheet must have 1 to 256 cells");
			}
			if (e.Renderer == ParticleRenderer::Ribbon && (e.RibbonSegments < 2 || e.RibbonSegments > PARTICLE_MAX_RIBBON_SEGMENTS))
			{
				return bad("a ribbon has 2 to " + std::to_string(PARTICLE_MAX_RIBBON_SEGMENTS) + " segments");
			}
			if (!finite(e.MinSegmentLength) || e.MinSegmentLength < 0.0f || !finite(e.SoftDistance) || e.SoftDistance < 0.0f || !finite(e.Intensity) || e.Intensity < 0.0f)
			{
				return bad("minSegmentLength, softDistance and intensity must be zero or more");
			}
			if (!finite(e.Restitution) || e.Restitution < 0.0f || e.Restitution > 1.0f)
			{
				return bad("restitution must be 0 to 1");
			}
			if (!(e.Duration > 0.0f) || !finite(e.Duration) || !finite(e.StartDelay) || e.StartDelay < 0.0f)
			{
				return bad("duration must be above zero and delay zero or more");
			}
			if (!finite(e.ConeAngle) || e.ConeAngle < 0.0f || e.ConeAngle > 90.0f)
			{
				return bad("coneAngle must be 0 to 90 degrees");
			}
			if (!finite_vec(e.ShapeSize) || glm::any(glm::lessThan(e.ShapeSize, glm::vec3(0.0f))))
			{
				return bad("shapeSize must be zero or more");
			}
			if (!finite_vec(e.Gravity) || !finite_vec(e.StartColor) || !finite(e.Drag) || e.Drag < 0.0f || !finite(e.NoiseStrength) || !finite(e.NoiseFrequency))
			{
				return bad("gravity, color, drag and noise must be finite (drag zero or more)");
			}
			for (const ParticleBurst& burst : e.Bursts)
			{
				if (!finite(burst.Time) || burst.Time < 0.0f)
				{
					return bad("a burst needs a time of zero or more");
				}
			}

			std::string problem;
			const struct { const ParticleValue& Value; const char* What; bool Positive; } values[] = {
				{ e.Rate, "rate", false }, { e.Lifetime, "lifetime", true }, { e.StartSpeed, "speed", false }, { e.StartSize, "size", false },
				{ e.StartRotation, "rotation", false }, { e.SizeOverLife, "size over life", false }, { e.RotationSpeed, "rotation speed", false },
				{ e.AlphaOverLife, "alpha over life", false }, { e.FrameOverLife, "frame over life", false } };
			for (const auto& v : values)
			{
				if (!value_valid(v.Value, v.What, v.Positive, problem))
				{
					return bad(problem);
				}
			}

			float last = -1.0f;
			for (const ParticleColorKey& key : e.ColorOverLife)
			{
				if (!finite(key.Time) || key.Time < 0.0f || key.Time > 1.0f || key.Time <= last || !finite_vec(key.Color))
				{
					return bad("color keys need finite numbers and times that rise within 0 to 1");
				}
				last = key.Time;
			}
		}
		return true;
	}
}
