// Vanilla supply load/unload only shows on a Vehicle (or a part slotted into one) that is not destroyed.
// A semitrailer is no Vehicle, it rides behind its tractor. On a semitrailer the same check is made on
// the trailer instead, everything else (resources, transfer, UI text) stays vanilla.

class SST_SupplyActionHelper
{
	//------------------------------------------------------------------------------------------------
	//! The semitrailer the action sits on, null if it is somewhere else (vanilla vehicles)
	static SST_SemiTrailerComponent GetTrailer(IEntity owner)
	{
		if (!owner)
			return null;

		SST_SemiTrailerComponent trailer = SST_SemiTrailerComponent.Cast(owner.FindComponent(SST_SemiTrailerComponent));
		if (!trailer && owner.GetRootParent())
			trailer = SST_SemiTrailerComponent.Cast(owner.GetRootParent().FindComponent(SST_SemiTrailerComponent));

		return trailer;
	}

	//------------------------------------------------------------------------------------------------
	static bool CanShowOnTrailer(SST_SemiTrailerComponent trailer, IEntity user, SCR_ResourceComponent resources, EResourceType type)
	{
		ChimeraCharacter character = ChimeraCharacter.Cast(user);
		if (character && (character.IsInVehicle() || SCR_ScriptedUserAction.IsUserPlayingBlockingAnimation(character)))
			return false;

		if (!resources || !resources.IsResourceTypeEnabled(type))
			return false;

		return !trailer.IsWrecked();
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_ResourceContainerVehicleLoadAction
{
	override bool CanBeShownScript(IEntity user)
	{
		SST_SemiTrailerComponent trailer = SST_SupplyActionHelper.GetTrailer(GetOwner());
		if (!trailer)
			return super.CanBeShownScript(user);

		return SST_SupplyActionHelper.CanShowOnTrailer(trailer, user, m_ResourceComponent, m_eResourceType);
	}
}

//------------------------------------------------------------------------------------------------
modded class SCR_ResourceContainerVehicleUnloadAction
{
	override bool CanBeShownScript(IEntity user)
	{
		SST_SemiTrailerComponent trailer = SST_SupplyActionHelper.GetTrailer(GetOwner());
		if (!trailer)
			return super.CanBeShownScript(user);

		return SST_SupplyActionHelper.CanShowOnTrailer(trailer, user, m_ResourceComponent, m_eResourceType);
	}
}
