#include "FloatingDamage.h"

static RE::GMatrix3D* worldToCamMatrix;
static bool initialized = false;
static std::vector<FloatingDamage::DisplayText> displayList;
static RE::TESCondition* hasLOSCondition;

void FloatingDamage::Initialize( REL::Version a_ver )
{
	worldToCamMatrix = (RE::GMatrix3D*)RelocationHelper::Get( RelocationID::WorldToCameraMatrix, a_ver ).address();

	hasLOSCondition = new RE::TESCondition;
	RE::TESConditionItem* hasLOS = new RE::TESConditionItem;
	hasLOS->data.comparisonValue.f = 1.0f;
	hasLOS->data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetLineOfSight;
	hasLOSCondition->head = hasLOS;
}

RE::GFxMovie* FloatingDamage::GetMenu()
{
	auto menu = RE::UI::GetSingleton()->GetMenu( "Floating Damage V2" );
	if( menu )
		return menu->uiMovie.get();

	return nullptr;
}

bool FloatingDamage::CreateFloatingText( const char* a_text, unsigned int a_color, int a_size )
{
	auto menu = GetMenu();
	if( menu == nullptr || worldToCamMatrix == nullptr )
		return false;

	DisplayText text;
	text.text	= a_text;
	text.color	= a_color;
	text.size	= a_size;

	displayList.push_back( text );

	return true;
}

void FloatingDamage::Flush( RE::TESObjectREFR* a_target, RE::NiPoint3* a_location, float a_offsetX, float a_offsetY, float a_alpha )
{
	if( displayList.empty() )
		return;

	bool hasTargetLOS = true;

	auto player = RE::PlayerCharacter::GetSingleton();
	if( a_target && a_target != player )
		hasTargetLOS = hasLOSCondition->IsTrue( player, a_target );

	auto menu = GetMenu();
	if( menu == nullptr || worldToCamMatrix == nullptr || !hasTargetLOS )
	{
		displayList.clear();
		return;
	}

	static RE::NiRect<float> viewport = { 0, 1, 0, 1 };

	float x = 0.5;
	float y = 0.75;
	float z = 0.5;
	if( a_location )
	{
		RE::NiPoint3 pos = *a_location;
		RE::NiCamera::WorldPtToScreenPt3( worldToCamMatrix->data, viewport, pos, x, y, z, 0.00001f );
	}

	double dist = z >= 1 ? 1E10f : 1 / (1.0 - z);
	double scale = 40.0 / dist * 100.0;
	scale = min( max( 75, scale ), 150 );

	RE::GFxValue args[ 8 ];
	menu->CreateArray( &args[ 0 ] );
	menu->CreateArray( &args[ 1 ] );
	menu->CreateArray( &args[ 2 ] );

	for( auto& text : displayList )
	{
		args[ 0 ].PushBack( text.text );
		args[ 1 ].PushBack( text.size );
		args[ 2 ].PushBack( text.color );
	}

	args[ 3 ].SetNumber( x + a_offsetX * scale / 100 );
	args[ 4 ].SetNumber( y + a_offsetY * scale / 100 );
	args[ 5 ].SetNumber( scale );
	args[ 6 ].SetNumber( a_alpha );
	args[ 7 ].SetBoolean( true );

	menu->Invoke( "_root.widget.PopupText", nullptr, args, 8 );

	displayList.clear();
}
