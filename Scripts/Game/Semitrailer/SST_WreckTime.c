// Remembers when a vehicle got destroyed, so the carrier trailer knows when the wreck is done
// (vanilla swaps the wreck model in 250 ms after the explosion, after the players got out).

modded class SCR_VehicleDamageManagerComponent
{
	protected float m_fSST_DestroyedAt = -1;

	//------------------------------------------------------------------------------------------------
	override void OnDamageStateChanged(EDamageState newState, EDamageState previousDamageState, bool isJIP)
	{
		super.OnDamageStateChanged(newState, previousDamageState, isJIP);

		// joined after it blew up: it is an old wreck
		if (newState != EDamageState.DESTROYED || isJIP)
		{
			m_fSST_DestroyedAt = -1;
			return;
		}

		IEntity owner = GetOwner();
		if (owner && owner.GetWorld())
			m_fSST_DestroyedAt = owner.GetWorld().GetWorldTime();
	}

	//------------------------------------------------------------------------------------------------
	//! Milliseconds since it was destroyed, -1 when not destroyed here
	float SST_GetTimeSinceDestroyed()
	{
		IEntity owner = GetOwner();
		if (m_fSST_DestroyedAt < 0 || !owner || !owner.GetWorld())
			return -1;

		return owner.GetWorld().GetWorldTime() - m_fSST_DestroyedAt;
	}
}
