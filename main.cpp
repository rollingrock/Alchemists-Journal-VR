#include "skse64_common/skse_version.h"
#include "skse64_common/Relocation.h"
#include "skse64/PluginAPI.h"
#include "skse64/GameData.h"
#include "skse64/GameMenus.h"
#include "skse64/PapyrusNativeFunctions.h"
#include "skse64/GameStreams.h"
#include "detours/include/detours.h"
#include <shlobj.h>
#include <vector>
#include <string>
#include <algorithm>


IDebugLog gLog;
PluginHandle g_pluginHandle = kPluginHandle_Invalid;
SKSEPapyrusInterface* g_papyrus = NULL;
SKSEMessagingInterface* g_messaging = NULL;


namespace AlchemistJournal
{

std::map<std::string, std::string> Translation = {
	{"undefined", "undefined"},
	{"average", "average"},
	{"weak", "weak"},
	{"very weak", "very weak"},
	{"strong", "strong"},
	{"very strong", "very strong"},
	{"short", "short"},
	{"very short", "very short"},
	{"long", "long"},
	{"very long", "very long"}
};

enum
{
	IngredientsSorting_Name = 0,
	IngredientsSorting_MagnitudeAsc,
	IngredientsSorting_MagnitudeDesc,
	IngredientsSorting_DurationAsc,
	IngredientsSorting_DurationDesc
};

enum
{
	ShowMagnitudeDuration_DontShow = 0,
	ShowMagnitudeDuration_Approximate,
	ShowMagnitudeDuration_RawNumbers
};

UInt32 FontSize = 20;
UInt32 IngredientsSorting = IngredientsSorting_Name;
UInt32 ShowMagnitudeDuration = ShowMagnitudeDuration_DontShow;
bool ShowUnknown = false;


class JournalGenerator
{
public:
	void			Run(std::string& text);

private:
	enum
	{
		ApproximateValue_VeryLow = 0,
		ApproximateValue_Low,
		ApproximateValue_Average,
		ApproximateValue_High,
		ApproximateValue_VeryHigh
	};

	struct JournalDataEntry
	{
		UInt32		ingredientId;
		const char*	ingredientName;
		UInt32		effectId;
		const char*	effectName;
		const char*	effectDescription;
		float		magnitude;
		UInt8		magnitudeApproximate;
		UInt32		duration;
		UInt8		durationApproximate;
		bool		isKnown;
	};

	typedef std::vector<JournalDataEntry> JournalData;

	void			GetData(JournalData& data);
	void			PrintData(const JournalData& data);
	void			SortData(JournalData& data);
	void			CalcApproximateValues(JournalData& data);
	void			AddEffectName(std::string& text, const JournalDataEntry& entry);
	void			AddEffectDescription(std::string& text, const JournalDataEntry& entry);
	void			AddIngredient(std::string& text, const JournalDataEntry& entry);
	UInt8			ApproximateValue(float value, float average);
	const char*		ApproximateMagnitudeStr(UInt8 value);
	const char*		ApproximateDurationStr(UInt8 value);
};

void JournalGenerator::Run(std::string& text)
{
	text.clear();

	JournalData journalData;
	GetData(journalData);
	SortData(journalData);
	CalcApproximateValues(journalData);

	text += "<font face='$HandwrittenFont'";
	text += " size='";
	text += std::to_string(FontSize);
	text += "'>";

	for (auto entry = std::begin(journalData); entry != std::end(journalData);)
	{
		UInt32 effectId = entry->effectId;

		std::string pageText;

		AddEffectName(pageText, *entry);
		AddEffectDescription(pageText, *entry);

		bool ingredientsListEmpty = true;
		for (; entry->effectId == effectId && entry != std::end(journalData); entry++)
		{
			if (!ShowUnknown && !entry->isKnown)
				continue;

			AddIngredient(pageText, *entry);
			ingredientsListEmpty = false;
		}

		if (ingredientsListEmpty)
			continue;

		pageText += "[pagebreak]\n";

		text += pageText;
	}

	text += "</font>";
}

void JournalGenerator::GetData(JournalData& data)
{
	data.clear();

	DataHandler* dataHandler = DataHandler::GetSingleton();
	if (dataHandler == NULL)
		return;

	JournalDataEntry entry;

	for (int ingrIdx = 0; ingrIdx < dataHandler->ingredients.count; ingrIdx++)
	{
		IngredientItem* ingredient = dataHandler->ingredients[ingrIdx];
		if (ingredient == NULL)
			continue;

		entry.ingredientId = ingredient->formID;

		entry.ingredientName = ingredient->fullName.GetName();
		if (entry.ingredientName == NULL || *entry.ingredientName == 0)
			continue;

		for (int effIdx = 0; effIdx < ingredient->effectItemList.count; effIdx++)
		{
			MagicItem::EffectItem* effect = ingredient->effectItemList[effIdx];
			if (effect == NULL || effect->mgef == NULL)
				continue;

			entry.effectId = effect->mgef->formID;

			entry.effectName = effect->mgef->fullName.GetName();
			if (entry.effectName == NULL || *entry.effectName == 0)
				continue;

			entry.effectDescription = effect->mgef->description.data != NULL ? effect->mgef->description.data : "";
			entry.magnitude = effect->magnitude;
			entry.duration = effect->duration;
			entry.isKnown = (ingredient->knownEffects & 1 << effIdx) != 0;

			data.push_back(entry);
		}
	}
}

void JournalGenerator::PrintData(const JournalData& data)
{
	for (const auto &entry : data)
		_MESSAGE("%08X\t%-30s\t%08X\t%-20s\t%7.2f\t%-10s\t%5d\t%-10s\t%-10s\t%s", entry.ingredientId, entry.ingredientName, entry.effectId, entry.effectName, entry.magnitude, ApproximateMagnitudeStr(entry.magnitudeApproximate), entry.duration, ApproximateDurationStr(entry.durationApproximate), entry.isKnown ? "known" : "unknown", entry.effectDescription);
}

void JournalGenerator::SortData(JournalData& data)
{
	std::sort(std::begin(data), std::end(data), [](const JournalDataEntry& left, const JournalDataEntry& right)
	{
		float diff = strcmp(left.effectName, right.effectName);
		if (diff == 0)
			diff = left.effectId - right.effectId;
		if (diff == 0)
			switch (IngredientsSorting)
			{
				case IngredientsSorting_MagnitudeAsc: diff = left.magnitude - right.magnitude; break;
				case IngredientsSorting_MagnitudeDesc: diff = right.magnitude - left.magnitude; break;
				case IngredientsSorting_DurationAsc: diff = (SInt32)left.duration - (SInt32)right.duration; break;
				case IngredientsSorting_DurationDesc: diff = (SInt32)right.duration - (SInt32)left.duration; break;
			}
		if (diff == 0)
			diff = strcmp(left.ingredientName, right.ingredientName);

		return diff < 0;
	});
}

void JournalGenerator::CalcApproximateValues(JournalData& data)
{
	std::map<UInt32, std::vector<float>> magnitudeValues;

	for (auto &entry : data)
		magnitudeValues[entry.effectId].push_back(entry.magnitude);

	for (auto &values : magnitudeValues)
		std::sort(std::begin(values.second), std::end(values.second));

	for (auto &entry : data)
	{
		auto &values = magnitudeValues[entry.effectId];
		float median = values[(values.size() - 1) / 2];
		entry.magnitudeApproximate = ApproximateValue(entry.magnitude, median);
	}

	std::map<UInt32, std::vector<UInt32>> durationValues;

	for (auto &entry : data)
		durationValues[entry.effectId].push_back(entry.duration);

	for (auto &values : durationValues)
		std::sort(std::begin(values.second), std::end(values.second));

	for (auto &entry : data)
	{
		auto &values = durationValues[entry.effectId];
		UInt32 median = values[(values.size() - 1) / 2];
		entry.durationApproximate = ApproximateValue(entry.duration, median);
	}
}

void JournalGenerator::AddEffectName(std::string& text, const JournalDataEntry& entry)
{
	text += "<u>";
	text += entry.effectName;
	text += "</u>";
	text += "\n\n";
}

void JournalGenerator::AddEffectDescription(std::string& text, const JournalDataEntry& entry)
{
	std::string effectDescription = entry.effectDescription;
	bool hasMag = effectDescription.find("<mag>") != std::string::npos;
	for (auto pos = effectDescription.find("<mag>"); pos != std::string::npos; pos = effectDescription.find("<mag>", pos))
		effectDescription.replace(pos, 5, "A");
	for (auto pos = effectDescription.find("<dur>"); pos != std::string::npos; pos = effectDescription.find("<dur>", pos))
		effectDescription.replace(pos, 5, hasMag ? "B" : "A");
	for (auto pos = effectDescription.find_first_of("<>"); pos < effectDescription.size() && pos != std::string::npos; pos = effectDescription.find_first_of("<>", pos))
		effectDescription.erase(pos, 1);

	text += effectDescription;
	text += "\n\n";
}

void JournalGenerator::AddIngredient(std::string& text, const JournalDataEntry& entry)
{
	text += "~";
	text += entry.ingredientName;

	if (ShowMagnitudeDuration != ShowMagnitudeDuration_DontShow)
	{
		std::string effectDescription = entry.effectDescription;
		bool hasMag = effectDescription.find("<mag>") != std::string::npos;
		bool hasDur = effectDescription.find("<dur>") != std::string::npos;

		if (ShowMagnitudeDuration == ShowMagnitudeDuration_RawNumbers)
		{
			if (hasMag || hasDur)
			{
				text += "      ";
				text += "<font color='#505050' size='";
				text += std::to_string(FontSize);
				text += "'>";
				text += "(";

				if (hasMag)
				{
					char magStr[10];
					sprintf_s(magStr, sizeof(magStr), "%g", entry.magnitude);
					text += magStr;
				}

				if (hasMag && hasDur)
					text += "/";

				if (hasDur)
				{
					char durStr[10];
					sprintf_s(durStr, sizeof(durStr), "%d", entry.duration);
					text += durStr;
				}

				text += ")";
				text += "</font>";
			}
		}
		else if (ShowMagnitudeDuration == ShowMagnitudeDuration_Approximate)
		{
			if (hasMag || hasDur)
			{
				text += "      ";
				text += "<font color='#505050' size='";
				text += std::to_string(FontSize - 2);
				text += "'>";
				text += "(";

				if (hasMag)
					text += ApproximateMagnitudeStr(entry.magnitudeApproximate);

				if (hasMag && hasDur)
					text += "/";

				if (hasDur)
					text += ApproximateDurationStr(entry.durationApproximate);

				text += ")";
				text += "</font>";
			}
		}
	}

	text += "\n";
}

UInt8 JournalGenerator::ApproximateValue(float value, float average)
{
	if (average == 0.0 && value == 0.0)
		return ApproximateValue_Average;

	if (value <= average * pow(2, -1.5))
		return ApproximateValue_VeryLow;
	if (value <= average * pow(2, -0.5))
		return ApproximateValue_Low;
	if (value < average * pow(2, 0.5))
		return ApproximateValue_Average;
	if (value < average * pow(2, 1.5))
		return ApproximateValue_High;

	return ApproximateValue_VeryHigh;
}

const char* JournalGenerator::ApproximateMagnitudeStr(UInt8 value)
{
	switch (value)
	{
		case ApproximateValue_VeryLow: return Translation["very weak"].c_str();
		case ApproximateValue_Low: return Translation["weak"].c_str();
		case ApproximateValue_Average: return Translation["average"].c_str();
		case ApproximateValue_High: return Translation["strong"].c_str();
		case ApproximateValue_VeryHigh: return Translation["very strong"].c_str();
	}

	return Translation["undefined"].c_str();
}

const char* JournalGenerator::ApproximateDurationStr(UInt8 value)
{
	switch (value)
	{
		case ApproximateValue_VeryLow: return Translation["very short"].c_str();
		case ApproximateValue_Low: return Translation["short"].c_str();
		case ApproximateValue_Average: return Translation["average"].c_str();
		case ApproximateValue_High: return Translation["long"].c_str();
		case ApproximateValue_VeryHigh: return Translation["very long"].c_str();
	}

	return Translation["undefined"].c_str();
}


RelocAddr<uintptr_t> SetBookText_Addr = 0x882840;     // SSE is 858530
RelocPtr<BSString> BookText = 0x3011218;


// ?
class UnkObject
{
public:
	void**			_vtbl;			// 00
	UInt8			unk4[0x70];		// 08
	GFxMovieView*	bookView;		// 78   // SSE was offset 68.    VR is offset 78
	UInt8			unk3C[0x25];	// 80   // SSE was offset 70
	bool			isNote;			// a5   // SSE was offset 95

	//MEMBER_FN_PREFIX(UnkObject);
	//DEFINE_MEMBER_FN(SetBookText, void, 0x00858530);

	typedef void (UnkObject::* SetBookText_Type)();

	inline void		SetBookText() { (this->*(SetBookText_Type&)SetBookText_Addr)(); };
	void			SetBookText_Hook();
};

STATIC_ASSERT(offsetof(UnkObject, bookView) == 0x78);
STATIC_ASSERT(offsetof(UnkObject, isNote) == 0xa5);

const char* GetJournalText()
{
	static std::string journalText;

	JournalGenerator journalGenerator;
	journalGenerator.Run(journalText);

	return journalText.c_str();
}

void UnkObject::SetBookText_Hook()
{
	if (BookText->Get() != NULL && strncmp(BookText->Get(), "#AlchemistJournal", sizeof(BookText->Get())) == 0)
	{
		_MESSAGE("hook if start");
		if (bookView != NULL)
		{
			FxResponseArgs<2> args;
			args.args[0].SetUndefined();
			args.args[1].SetString(GetJournalText());
			args.args[2].SetBool(isNote);
			
			_MESSAGE("before invoke");

			InvokeFunction(bookView, "SetBookText", &args);
		}
		_MESSAGE("hook if end");

	}
	else {
		// Calling original function.
		_MESSAGE("hook else start");
		SetBookText();
		_MESSAGE("hook else end");

	}
}


void ApplyPatch()
{
	DetourTransactionBegin();
	DetourUpdateThread(GetCurrentThread());
	DetourAttach((PVOID*)&SetBookText_Addr, (PVOID)GetFnAddr(&UnkObject::SetBookText_Hook));
	LONG result = DetourTransactionCommit();

	if (result != NO_ERROR)
		_MESSAGE("Hook installation failed, error code: %d.", result);
}

void SetFontSize(StaticFunctionTag* base, UInt32 fontSize)
{
	FontSize = fontSize;
}

void SetSorting(StaticFunctionTag* base, UInt32 sorting)
{
	IngredientsSorting = sorting;
}

void SetShowMagnitudeDuration(StaticFunctionTag* base, UInt32 value)
{
	ShowMagnitudeDuration = value;
}

void SetShowUnknown(StaticFunctionTag* base, bool show)
{
	ShowUnknown = show;
}

bool RegisterFuncs(VMClassRegistry* registry)
{
	registry->RegisterFunction(
		new NativeFunction1<StaticFunctionTag, void, UInt32>("SetFontSize", "ALCJRN_PluginScript", SetFontSize, registry));
	registry->RegisterFunction(
		new NativeFunction1<StaticFunctionTag, void, UInt32>("SetSorting", "ALCJRN_PluginScript", SetSorting, registry));
	registry->RegisterFunction(
		new NativeFunction1<StaticFunctionTag, void, UInt32>("SetShowMagnitudeDuration", "ALCJRN_PluginScript", SetShowMagnitudeDuration, registry));
	registry->RegisterFunction(
		new NativeFunction1<StaticFunctionTag, void, bool>("SetShowUnknown", "ALCJRN_PluginScript", SetShowUnknown, registry));

	return true;
}

void ReadTranslation()
{
	std::string language = "ENGLISH";

	Setting* languageSetting = GetINISetting("sLanguage:General");
	if (languageSetting != NULL && languageSetting->GetType() == Setting::kType_String && languageSetting->data.s != NULL)
		language = languageSetting->data.s;

	std::string path = "Interface\\Translations\\alchemistjournal_" + language + "_2.txt";

	BSResourceNiBinaryStream fileStream(path.c_str());
	if (!fileStream.IsValid())
		return;

	while (true)
	{
		char buf[512];
		UInt32 len = fileStream.ReadLine(buf, sizeof(buf), '\n');

		if (len == 0)
			break;

		if (buf[len - 1] == '\r')
			len--;

		buf[len] = 0;

		char* delim = strchr(buf, '\t');
		if (delim == NULL)
			continue;

		std::string key(buf, delim - buf);
		std::string value(delim + 1);

		if (Translation.find(key) != std::end(Translation))
			Translation[key] = value;
	}
}

void OnSKSEMessage(SKSEMessagingInterface::Message* message)
{
	if (message != NULL && message->type == SKSEMessagingInterface::kMessage_DataLoaded)
		ReadTranslation();
}

} // namespace AlchemistJournal


extern "C"
{

	bool SKSEPlugin_Query(const SKSEInterface * skse, PluginInfo * info)
	{
		gLog.OpenRelative(CSIDL_MYDOCUMENTS, "\\My Games\\Skyrim VR\\SKSE\\alchemistjournal.log");

		//_MESSAGE("query");

		info->infoVersion = PluginInfo::kInfoVersion;
		info->name = "AlchemistJournal";
		info->version = 1;

		g_pluginHandle = skse->GetPluginHandle();

		if (skse->isEditor)
		{
			_MESSAGE("Loaded in editor, marking as incompatible.");

			return false;
		}

		if (skse->runtimeVersion != RUNTIME_VR_VERSION_1_4_15)
		{
			_MESSAGE("Unsupported runtime version %08X.", skse->runtimeVersion);

			return false;
		}

		g_papyrus = (SKSEPapyrusInterface*)skse->QueryInterface(kInterface_Papyrus);
		if (g_papyrus == NULL)
		{
			_MESSAGE("Couldn't get papyrus interface.");

			return false;
		}

		if (g_papyrus->interfaceVersion < SKSEPapyrusInterface::kInterfaceVersion)
		{
			_MESSAGE("Papyrus interface too old (%d expected %d).", g_papyrus->interfaceVersion, SKSEPapyrusInterface::kInterfaceVersion);

			return false;
		}

		g_messaging = (SKSEMessagingInterface*)skse->QueryInterface(kInterface_Messaging);
		if (g_messaging == NULL)
		{
			_MESSAGE("Couldn't get messaging interface.");

			return false;
		}

		if (g_messaging->interfaceVersion < SKSEMessagingInterface::kInterfaceVersion)
		{
			_MESSAGE("Messaging interface too old (%d expected %d).", g_messaging->interfaceVersion, SKSEMessagingInterface::kInterfaceVersion);

			return false;
		}

		return true;
	}

	bool SKSEPlugin_Load(const SKSEInterface * skse)
	{
		_MESSAGE("load");

		AlchemistJournal::ApplyPatch();
		g_papyrus->Register(AlchemistJournal::RegisterFuncs);
		g_messaging->RegisterListener(g_pluginHandle, "SKSE", AlchemistJournal::OnSKSEMessage);

		return true;
	}

};