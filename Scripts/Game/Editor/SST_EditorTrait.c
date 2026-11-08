// Game Master trait "Steel's Trucks", so everything from this mod can be filtered in one click.
// The value is added to the label enum, the editor gives unknown labels a default setting at start-up
// and the overrides below put that one into the Trait group with a name and an icon.

modded enum EEditableEntityLabel
{
	TRAIT_STEELS_TRUCKS = 13101,
}

// The attribute line has to be repeated, otherwise the class stops being a config class and the
// labels from EditableEntityCore.conf fail to load.
[BaseContainerProps(), SCR_BaseContainerCustomTitleEnum(EEditableEntityLabel, "m_LabelType")]
modded class SCR_EditableEntityCoreLabelSetting
{
	protected static ref SCR_UIInfo s_SSTTraitInfo;

	//------------------------------------------------------------------------------------------------
	protected bool SST_IsTrait()
	{
		return GetLabelType() == EEditableEntityLabel.TRAIT_STEELS_TRUCKS;
	}

	//------------------------------------------------------------------------------------------------
	override SCR_UIInfo GetInfo()
	{
		if (!SST_IsTrait())
			return super.GetInfo();

		if (!s_SSTTraitInfo)
			s_SSTTraitInfo = SCR_UIInfo.CreateInfo("Steel's Trucks", "Tractors and semitrailers from Steel's semitrailer and towing.", "{5D1E7A93C40B28F6}UI/Textures/Editor/SST_Trait_SteelsTrucks.edds");

		return s_SSTTraitInfo;
	}

	//------------------------------------------------------------------------------------------------
	override EEditableEntityLabelGroup GetLabelGroupType()
	{
		if (SST_IsTrait())
			return EEditableEntityLabelGroup.TRAIT;

		return super.GetLabelGroupType();
	}

	//------------------------------------------------------------------------------------------------
	override bool GetFilterEnabled()
	{
		if (SST_IsTrait())
			return true;

		return super.GetFilterEnabled();
	}
}
