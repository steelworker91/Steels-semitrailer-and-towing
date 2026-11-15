// Supplies take room in the box of a supply semitrailer: the more supplies are loaded, the less room is
// left for items. Full of supplies no item fits in. Items never limit the supplies, a full box of items
// still takes all 4000. Vanilla never links the two (M923A1 keeps its trunk and supplies apart), this is
// only for the semitrailer.
//
// How full of supplies the box is gets replicated, so a client sees in the inventory right away whether
// an item still fits.

class SST_SupplyCargoStorageComponentClass : SCR_UniversalInventoryStorageComponentClass
{
}

class SST_SupplyCargoStorageComponent : SCR_UniversalInventoryStorageComponent
{
	//------------------------------------------------------------------------------------------------
	//! Volume items may take with the supplies loaded right now
	float GetItemRoom()
	{
		float room = GetMaxVolumeCapacity();

		SST_SemiTrailerComponent trailer = SST_SemiTrailerComponent.Cast(GetOwner().FindComponent(SST_SemiTrailerComponent));
		if (trailer)
			room *= 1 - trailer.SST_GetSupplyFill();

		return room;
	}

	//------------------------------------------------------------------------------------------------
	protected bool HasRoomFor(float volume)
	{
		if (GetOccupiedSpace() + volume <= GetItemRoom())
			return true;

		if (pInventoryManager)
			pInventoryManager.SetReturnCode(EInventoryRetCode.RETCODE_ITEM_TOO_BIG);

		return false;
	}

	//------------------------------------------------------------------------------------------------
	override bool CanStoreItem(IEntity item, int slotID)
	{
		if (!super.CanStoreItem(item, slotID))
			return false;

		// moved inside the box, already counted
		if (Contains(item))
			return true;

		return HasRoomFor(GetItemComponent(item).GetTotalVolume());
	}

	//------------------------------------------------------------------------------------------------
	override bool CanReplaceItem(IEntity nextItem, int slotID)
	{
		if (!super.CanReplaceItem(nextItem, slotID))
			return false;

		float change = GetItemComponent(nextItem).GetTotalVolume() - GetItemComponent(Get(slotID)).GetTotalVolume();
		return HasRoomFor(change);
	}
}

//------------------------------------------------------------------------------------------------
modded class SST_SemiTrailerComponent
{
	//! 0 = no supplies, 1 = box full of supplies
	[RplProp()]
	protected float m_fSST_SupplyFill;

	// server only
	protected SCR_ResourceContainer m_SST_SupplyContainer;
	protected float m_fSST_SupplyMax;

	//------------------------------------------------------------------------------------------------
	override void OnPostInit(IEntity owner)
	{
		super.OnPostInit(owner);
		if (!GetGame() || !GetGame().InPlayMode() || !Replication.IsServer())
			return;

		if (owner.FindComponent(SST_SupplyCargoStorageComponent))
			GetGame().GetCallqueue().CallLater(SST_HookSupplies, 0, false);
	}

	//------------------------------------------------------------------------------------------------
	override void OnDelete(IEntity owner)
	{
		if (m_SST_SupplyContainer)
			m_SST_SupplyContainer.GetOnResourcesChanged().Remove(SST_OnSuppliesChanged);

		if (GetGame() && GetGame().GetCallqueue())
			GetGame().GetCallqueue().Remove(SST_HookSupplies);

		super.OnDelete(owner);
	}

	//------------------------------------------------------------------------------------------------
	float SST_GetSupplyFill()
	{
		return m_fSST_SupplyFill;
	}

	//------------------------------------------------------------------------------------------------
	protected void SST_HookSupplies()
	{
		SCR_ResourceComponent resources = SCR_ResourceComponent.Cast(GetOwner().FindComponent(SCR_ResourceComponent));
		if (!resources)
			return;

		m_SST_SupplyContainer = resources.GetContainer(EResourceType.SUPPLIES);
		if (!m_SST_SupplyContainer)
			return;

		m_fSST_SupplyMax = m_SST_SupplyContainer.GetMaxResourceValue();
		m_SST_SupplyContainer.GetOnResourcesChanged().Insert(SST_OnSuppliesChanged);

		// supplies may already be there (save loaded)
		SST_OnSuppliesChanged();
	}

	//------------------------------------------------------------------------------------------------
	protected void SST_OnSuppliesChanged()
	{
		float fill = 0;
		if (m_fSST_SupplyMax > 0)
			fill = Math.Clamp(m_SST_SupplyContainer.GetResourceValue() / m_fSST_SupplyMax, 0, 1);

		if (fill == m_fSST_SupplyFill)
			return;

		m_fSST_SupplyFill = fill;
		Replication.BumpMe();
	}
}

//------------------------------------------------------------------------------------------------
//! The volume bar of the box shows the room the supplies leave for items
modded class SCR_InventoryStorageBaseUI
{
	override float GetMaxVolumeCapacity(BaseInventoryStorageComponent storage)
	{
		SST_SupplyCargoStorageComponent cargo = SST_SupplyCargoStorageComponent.Cast(storage);
		if (cargo)
			return cargo.GetItemRoom();

		return super.GetMaxVolumeCapacity(storage);
	}
}
