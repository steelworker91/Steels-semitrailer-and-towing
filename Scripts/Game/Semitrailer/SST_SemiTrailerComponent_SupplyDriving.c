// Supply load/unload of a semitrailer is switched off while it rolls.
//
// The vanilla load/unload consumers and generators look for supply containers around them every 2.5 m
// their owner moves and replicate their state after each look. Loading and unloading only make sense
// standing, so while the trailer moves the interactors let go of the containers they are linked to and
// their rights are set to NONE: an isolated interactor is skipped by the resource grid
// (SCR_ResourceActor.IsIsolated, SCR_ResourceGrid.UpdateInteractor). Standing again, they get their
// rights back, the grid links them anew on their next update, and the trailer is put where it is now
// into the grid.
//
// Every machine does this for its own copy of the trailer, from how fast that copy moves.

modded class SST_SemiTrailerComponent
{
	protected static const float SUPPLY_CHECK_INTERVAL = 0.25;	// s
	protected static const float SUPPLY_OFF_SPEED = 1.0;		// m/s, switched off above
	protected static const float SUPPLY_ON_SPEED = 0.3;			// m/s, back on below
	protected static const int SUPPLY_ON_CHECKS = 4;			// that many checks in a row (1 s)

	protected SCR_ResourceComponent m_SupplyResources;
	protected bool m_bSupplyChecked;
	protected bool m_bSupplyOff;
	protected float m_fSupplyTimer;
	protected vector m_vSupplyLastPos;
	protected int m_iSupplySlowChecks;
	protected ref array<SCR_ResourceInteractor> m_aSupplyInteractors = {};
	protected ref array<int> m_aSupplyRights = {};

	//------------------------------------------------------------------------------------------------
	override void EOnFrame(IEntity owner, float timeSlice)
	{
		super.EOnFrame(owner, timeSlice);

		if (!m_bSupplyChecked)
		{
			m_bSupplyChecked = true;
			m_SupplyResources = SCR_ResourceComponent.Cast(owner.FindComponent(SCR_ResourceComponent));
			m_vSupplyLastPos = owner.GetOrigin();
		}

		if (!m_SupplyResources)
			return;

		m_fSupplyTimer += timeSlice;
		if (m_fSupplyTimer < SUPPLY_CHECK_INTERVAL)
			return;

		vector pos = owner.GetOrigin();
		float speed = vector.Distance(pos, m_vSupplyLastPos) / m_fSupplyTimer;
		m_vSupplyLastPos = pos;
		m_fSupplyTimer = 0;

		if (speed < SUPPLY_ON_SPEED)
			m_iSupplySlowChecks++;
		else
			m_iSupplySlowChecks = 0;

		if (!m_bSupplyOff && speed > SUPPLY_OFF_SPEED)
			SST_SetSupplyActive(false);
		else if (m_bSupplyOff && m_iSupplySlowChecks >= SUPPLY_ON_CHECKS)
			SST_SetSupplyActive(true);
	}

	//------------------------------------------------------------------------------------------------
	protected void SST_SetSupplyActive(bool active)
	{
		m_bSupplyOff = !active;

		if (active)
		{
			foreach (int i, SCR_ResourceInteractor interactor : m_aSupplyInteractors)
			{
				if (interactor)
					interactor.SetResourceRights(m_aSupplyRights[i]);
			}

			m_aSupplyInteractors.Clear();
			m_aSupplyRights.Clear();

			// the grid still has the trailer where it stood before it drove off
			if (m_SupplyResources.GetGridUpdateId() != int.MIN)
				GetGame().GetResourceGrid().UpdateResourceItem(m_SupplyResources);

			return;
		}

		array<SCR_ResourceConsumer> consumers = m_SupplyResources.GetConsumers();
		if (consumers)
		{
			foreach (SCR_ResourceConsumer consumer : consumers)
			{
				SST_Isolate(consumer);
			}
		}

		array<SCR_ResourceGenerator> generators = m_SupplyResources.GetGenerators();
		if (generators)
		{
			foreach (SCR_ResourceGenerator generator : generators)
			{
				SST_Isolate(generator);
			}
		}
	}

	//------------------------------------------------------------------------------------------------
	protected void SST_Isolate(SCR_ResourceInteractor interactor)
	{
		if (!interactor || interactor.IsIsolated())
			return;

		m_aSupplyInteractors.Insert(interactor);
		m_aSupplyRights.Insert(interactor.GetResourceRight());
		interactor.SetResourceRights(EResourceRights.NONE);

		for (int i = interactor.GetContainerCount() - 1; i >= 0; i--)
		{
			interactor.UnregisterContainer(i);
		}
	}
}
