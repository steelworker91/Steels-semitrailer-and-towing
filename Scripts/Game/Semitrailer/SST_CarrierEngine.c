// A vehicle on the deck of a carrier keeps its engine off, it is cargo there and comes off with
// "Unload vehicle". Vanilla asks the controller before every start (OnBeforeEngineStart, cars and
// helicopters alike) and refuses a start with a dead battery the same way.

modded class VehicleControllerComponent
{
	//------------------------------------------------------------------------------------------------
	override bool OnBeforeEngineStart()
	{
		if (SST_VehicleCarrierComponent.IsOnDeck(GetOwner()))
			return false;

		return super.OnBeforeEngineStart();
	}
}
