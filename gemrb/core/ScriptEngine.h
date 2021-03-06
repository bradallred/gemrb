/* GemRB - Infinity Engine Emulator
 * Copyright (C) 2003 The GemRB Project
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 *
 */

#ifndef SCRIPTENGINE_H
#define SCRIPTENGINE_H

#include "Plugin.h"
#include "Resource.h"

#include <map>
#include <stdint.h>
#include <string>
#include <typeinfo>
#include <vector>

namespace GemRB {

class Point;

typedef uint64_t ScriptingId;
typedef std::string ScriptingClassId;

class ScriptingRefBase {
public:
	const ScriptingId Id; // unique id for each object in a ScriptingGroup

	ScriptingRefBase(ScriptingId id)
	: Id(id) {}

	virtual ~ScriptingRefBase() {};

	// key to separate groups of objects for faster searching and id collision prevention
	virtual const ResRef& ScriptingGroup() const=0;
	// class to instantiate on the script side (Python)
	virtual const ScriptingClassId ScriptingClass() const=0;
};

template <class T>
class ScriptingRef : public ScriptingRefBase {
private:
	T* ref;
public:
	typedef T* RefType;
	
	ScriptingRef(T* ref, ScriptingId id)
	: ScriptingRefBase(id), ref(ref) {}

	T* GetObject() const { return ref; }
};


class GEM_EXPORT ScriptEngine : public Plugin {
public:
	typedef std::map<ScriptingId, ScriptingRefBase*> ScriptingDefinitions;
	
private:
	typedef std::map<ResRef, ScriptingDefinitions> ScriptingDict;
	static ScriptingDict GUIDict;

public:
	static bool RegisterScriptingRef(ScriptingRefBase* ref);
	static bool UnregisterScriptingRef(ScriptingRefBase* ref);

	static ScriptingDefinitions GetScriptingGroup(ResRef groupId)
	{
		return GUIDict[groupId];
	}

	static ScriptingRefBase* GetScripingRef(ResRef group, ScriptingId id)
	{
		ScriptingRefBase* ref = NULL;
		ScriptingDefinitions::iterator it = GUIDict[group].find(id);
		if (it != GUIDict[group].end()) {
			ref = it->second;
		}
		return ref;
	}

	class Parameter {
		struct TypeInterface {
			virtual ~TypeInterface() {};
			virtual TypeInterface* Clone() const = 0;
			virtual const std::type_info& Type() const = 0;
		};

		template <typename T>
		struct ConcreteType : public TypeInterface {
			T value;
			ConcreteType(T value) : value(value) {}

			virtual TypeInterface *Clone() const
			{
				return new ConcreteType(value);
			}

			const std::type_info& Type() const {
				return typeid(T);
			}
		};

		TypeInterface* ptr;

	public:
		template <typename T>
		Parameter(T value) {
			ptr = new ConcreteType<T>(value);
		}

		Parameter() : ptr(NULL) {}

		Parameter( const Parameter& s ) {
			ptr = (s.ptr) ? s.ptr->Clone() : NULL;
		}

		Parameter& Swap(Parameter& rhs) {
			std::swap(ptr, rhs.ptr);
			return *this;
		}

		Parameter& operator=(const Parameter& rhs) {
			Parameter tmp(rhs);
			return Swap(tmp);
		}

		~Parameter() {
			delete ptr;
		}

		const std::type_info& Type() const {
			return (ptr) ? ptr->Type() : typeid(void);
		}

		template <typename T>
		const T& Value() const {
			ConcreteType<T>* type = dynamic_cast<ConcreteType<T>*>(ptr);
			if (type) {
				return type->value;
			}
			// default
			static T t;
			return t;
		}
	};

	typedef std::vector<Parameter> FunctionParameters;

	static const ScriptingId InvalidId = static_cast<ScriptingId>(-1);

public:
	ScriptEngine(void) {};
	virtual ~ScriptEngine(void) {};
	/** Initialization Routine */
	virtual bool Init(void) = 0;
	/** Load Script */
	virtual bool LoadScript(const char* filename) = 0;
	/** Run Function */
	virtual bool RunFunction(const char* Modulename, const char* FunctionName, const FunctionParameters& params, bool report_error = true) = 0;
	// TODO: eleminate these RunFunction variants.
	virtual bool RunFunction(const char *ModuleName, const char* FunctionName, bool report_error=true, int intparam=-1) = 0;
	virtual bool RunFunction(const char* Modulename, const char* FunctionName, bool report_error, Point) = 0;
	/** Exec a single String */
	virtual bool ExecString(const char* string, bool feedback) = 0;
};

}

#endif
