// Once the wreck model is in, the vehicle itself does not offer its actions anymore. Vanilla puts
// "Salvage" on a small entity it attaches to the wreck (VehicleSalvageActionEntity.et), the
// "Load wreck onto trailer" action is added to that entity (prefab override).
// Vanilla only spawns it when vehicle salvage is enabled in the game mode; the salvage action hides
// itself in that case anyway, so it is spawned always.

modded class SCR_SalvageVehicleSlotInfo
{
	//------------------------------------------------------------------------------------------------
	override void OnDamageStateChanged(EDamageState state)
	{
		super.OnDamageStateChanged(state);

		if (state == EDamageState.DESTROYED && !GetAttachedEntity())
			SpawnSalvageEntity();
	}
}
