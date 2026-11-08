[ComponentEditorProps(category: "SST/Semitrailer", description: "Damage of the fuel semitrailer, the full tank goes up in a chain of fuel explosions")]
class SST_TankerDamageManagerComponentClass : SCR_DamageManagerComponentClass
{
}

// The main blast comes from the vanilla fuel secondary explosion (the SST table has a tier above a full
// vanilla fuel truck). On top of that the tank bursts at the front and at the rear, one after the other,
// as long as there was enough fuel in it. Destroyed, the trailer keeps its models with burnt materials.
class SST_TankerDamageManagerComponent : SCR_DamageManagerComponent
{
	[Attribute("{72BEEF40AF179763}Prefabs/Weapons/Warheads/Explosions/Explosion_Tnt_Large.et", UIWidgets.ResourceNamePicker, "Explosion where the tank bursts", params: "et", category: "Tank burst")]
	protected ResourceName m_sBurstPrefab;

	[Attribute("0 1.4 -1.5", UIWidgets.Coords, "First burst, trailer space (front of the tank)", category: "Tank burst")]
	protected vector m_vBurstFront;

	[Attribute("0 1.4 -9", UIWidgets.Coords, "Second burst, trailer space (rear of the tank)", category: "Tank burst")]
	protected vector m_vBurstRear;

	[Attribute("300", UIWidgets.EditBox, "Delay between the bursts [ms]", category: "Tank burst")]
	protected int m_iBurstDelay;

	[Attribute("4551", UIWidgets.EditBox, "Fuel needed for the bursts [l] (more than a full vanilla fuel truck)", category: "Tank burst")]
	protected float m_fBurstMinFuel;

	[Attribute("{9BCA36D699046B50}Assets/Vehicles/Semitrailer/SST_TankWreck.emat", UIWidgets.ResourceNamePicker, "Burnt tank", params: "emat", category: "Wreck")]
	protected ResourceName m_sTankWreckMaterial;

	[Attribute("{3753149BD1AB2338}Assets/Vehicles/Semitrailer/SST_TrailerWreck.emat", UIWidgets.ResourceNamePicker, "Burnt trailer frame", params: "emat", category: "Wreck")]
	protected ResourceName m_sTrailerWreckMaterial;

	[Attribute("{A12D8821C60D8BB0}Assets/Vehicles/Semitrailer/SST_HubWreck.emat", UIWidgets.ResourceNamePicker, "Burnt wheel hubs", params: "emat", category: "Wreck")]
	protected ResourceName m_sHubWreckMaterial;

	[Attribute("{B57AF51778168639}Assets/Vehicles/Wheeled/M923A1/Data/M923A1_Wreck_Wheel.emat", UIWidgets.ResourceNamePicker, "Burnt wheels (vanilla M923A1 wreck wheel, same model)", params: "emat", category: "Wreck")]
	protected ResourceName m_sWheelWreckMaterial;

	[Attribute("{8FBC3A6E946F056E}Common/Materials/Default_Transparent.emat", UIWidgets.ResourceNamePicker, "Decals burn off", params: "emat", category: "Wreck")]
	protected ResourceName m_sDecalWreckMaterial;

	//------------------------------------------------------------------------------------------------
	protected override void OnDamageStateChanged(EDamageState newState, EDamageState previousDamageState, bool isJIP)
	{
		super.OnDamageStateChanged(newState, previousDamageState, isJIP);

		// the wreck look is local, every machine (and every joining player) sets it from the damage state
		if (newState == EDamageState.DESTROYED || previousDamageState == EDamageState.DESTROYED)
			SetWreck(newState == EDamageState.DESTROYED);

		// a wreck is not towed
		if (!isJIP && newState == EDamageState.DESTROYED && Replication.IsServer())
			DropFromTractor();

		if (isJIP || newState != EDamageState.DESTROYED || m_sBurstPrefab.IsEmpty())
			return;

		if (GetDefaultHitZone() && GetDefaultHitZone().IsProxy())
			return;

		float fuel;
		GetSecondaryExplosionPosition(SCR_FuelHitZone, fuel);
		if (fuel < m_fBurstMinFuel)
			return;

		GetGame().GetCallqueue().CallLater(Burst, m_iBurstDelay, false, m_vBurstFront);
		GetGame().GetCallqueue().CallLater(Burst, m_iBurstDelay * 2, false, m_vBurstRear);
	}

	//------------------------------------------------------------------------------------------------
	protected void DropFromTractor()
	{
		SST_SemiTrailerComponent trailer = SST_SemiTrailerComponent.Cast(GetOwner().FindComponent(SST_SemiTrailerComponent));
		if (!trailer || !trailer.GetTractor())
			return;

		SST_SemiTractorComponent tractor = SST_SemiTractorComponent.Cast(trailer.GetTractor().FindComponent(SST_SemiTractorComponent));
		if (tractor)
			tractor.Uncouple();
	}

	//------------------------------------------------------------------------------------------------
	protected void Burst(vector localPos)
	{
		IEntity owner = GetOwner();
		if (!owner || owner.IsDeleted())
			return;

		Instigator instigator = GetInstigator();
		if (!instigator)
			return;

		EntitySpawnParams params = new EntitySpawnParams();
		params.Transform[3] = localPos;
		SecondaryExplosion(m_sBurstPrefab, instigator, params);
	}

	//------------------------------------------------------------------------------------------------
	//! Burnt materials on the same models (the trailer xob, the vanilla wheels and the hubs), false puts
	//! the original materials back after a repair
	protected void SetWreck(bool wreck)
	{
		IEntity owner = GetOwner();
		if (!owner)
			return;

		RemapTrailer(owner, wreck);

		IEntity child = owner.GetChildren();
		while (child)
		{
			if (child.FindComponent(SST_TrailerWheelComponent))
			{
				RemapAll(child, wreck, m_sWheelWreckMaterial);

				IEntity hub = child.GetChildren();
				while (hub)
				{
					RemapAll(hub, wreck, m_sHubWreckMaterial);
					hub = hub.GetSibling();
				}
			}

			child = child.GetSibling();
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void RemapTrailer(IEntity ent, bool wreck)
	{
		VObject obj = ent.GetVObject();
		if (!obj)
			return;

		string remap;
		if (wreck)
		{
			string materials[256];
			int count = obj.GetMaterials(materials);
			for (int i = 0; i < count; i++)
			{
				string target = WreckMaterialFor(materials[i]);
				if (!target.IsEmpty())
					remap += string.Format("$remap '%1' '%2';", materials[i], target);
			}
		}

		ent.SetObject(obj, remap);
		ent.Update();
	}

	//------------------------------------------------------------------------------------------------
	protected void RemapAll(IEntity ent, bool wreck, ResourceName material)
	{
		VObject obj = ent.GetVObject();
		if (!obj)
			return;

		string remap;
		if (wreck && !material.IsEmpty())
		{
			string materials[256];
			int count = obj.GetMaterials(materials);
			for (int i = 0; i < count; i++)
			{
				remap += string.Format("$remap '%1' '%2';", materials[i], material);
			}
		}

		ent.SetObject(obj, remap);
		ent.Update();
	}

	//------------------------------------------------------------------------------------------------
	//! Material names of the tanker model: SST_Tank*, SST_Semitrailer* and the decal sheet
	protected ResourceName WreckMaterialFor(string material)
	{
		string name = material;
		name.ToLower();

		if (name.Contains("decal"))
			return m_sDecalWreckMaterial;

		if (name.Contains("tank"))
			return m_sTankWreckMaterial;

		if (name.Contains("semitrailer"))
			return m_sTrailerWreckMaterial;

		return ResourceName.Empty;
	}
}
