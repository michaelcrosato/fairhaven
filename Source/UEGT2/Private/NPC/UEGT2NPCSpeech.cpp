#include "NPC/UEGT2NPCSpeech.h"

#define LOCTEXT_NAMESPACE "UEGT2NPCSpeech"

// Named rather than anonymous: see the note in UEGT2NPCTypes.cpp.
namespace UEGT2Speech
{
	using FPool = TArray<FText>;

	/** Announce: what I am about to go and do. Indexed by EUEGT2Activity. */
	const TArray<FPool>& AnnounceTable()
	{
		static const TArray<FPool> Table = []
		{
			TArray<FPool> T;
			T.SetNum((int32)EUEGT2Activity::Count);

			T[(int32)EUEGT2Activity::Sleep] = {
				LOCTEXT("AnSleep1", "turning in. long day tomorrow"),
				LOCTEXT("AnSleep2", "lamp's out. see you in the morning"),
				LOCTEXT("AnSleep3", "bed. finally"),
				LOCTEXT("AnSleep4", "if anyone knocks, I'm not in"),
				LOCTEXT("AnSleep5", "shutters closed, door barred, done"),
			};
			T[(int32)EUEGT2Activity::Wake] = {
				LOCTEXT("AnWake1", "up. barely"),
				LOCTEXT("AnWake2", "who decided mornings"),
				LOCTEXT("AnWake3", "right. kettle on"),
				LOCTEXT("AnWake4", "one more minute. no. up"),
				LOCTEXT("AnWake5", "sun's up before me again"),
			};
			T[(int32)EUEGT2Activity::Breakfast] = {
				LOCTEXT("AnBreak1", "bread and whatever's left of the cheese"),
				LOCTEXT("AnBreak2", "eating, then out"),
				LOCTEXT("AnBreak3", "porridge. again"),
				LOCTEXT("AnBreak4", "quick bite then I'm off"),
			};
			T[(int32)EUEGT2Activity::Commute] = {
				LOCTEXT("AnCom1", "heading in. running a bit late"),
				LOCTEXT("AnCom2", "walking down. mind the puddle by the corner"),
				LOCTEXT("AnCom3", "off to open up"),
				LOCTEXT("AnCom4", "on my way. back tonight"),
				LOCTEXT("AnCom5", "same road, same hour, every day"),
			};
			T[(int32)EUEGT2Activity::Work] = {
				LOCTEXT("AnWork1", "back at it"),
				LOCTEXT("AnWork2", "this'll take all afternoon"),
				LOCTEXT("AnWork3", "nearly through the pile"),
				LOCTEXT("AnWork4", "don't mind me. working"),
				LOCTEXT("AnWork5", "get this done and I'm free by dark"),
			};
			T[(int32)EUEGT2Activity::Market] = {
				LOCTEXT("AnMkt1", "need bread, thread and something for the pot"),
				LOCTEXT("AnMkt2", "just having a look"),
				LOCTEXT("AnMkt3", "if the fish is fresh I'll take two"),
				LOCTEXT("AnMkt4", "prices are up again. of course they are"),
				LOCTEXT("AnMkt5", "in and out, I said. that was an hour ago"),
			};
			T[(int32)EUEGT2Activity::Lunch] = {
				LOCTEXT("AnLun1", "stopping for a bite"),
				LOCTEXT("AnLun2", "food. now"),
				LOCTEXT("AnLun3", "half an hour, then back to it"),
				LOCTEXT("AnLun4", "eating on my feet again"),
			};
			T[(int32)EUEGT2Activity::Errand] = {
				LOCTEXT("AnErr1", "have to drop this off. back shortly"),
				LOCTEXT("AnErr2", "quick errand, won't be long"),
				LOCTEXT("AnErr3", "one stop, then home"),
				LOCTEXT("AnErr4", "promised I'd fetch this hours ago"),
				LOCTEXT("AnErr5", "going the long way. no particular reason"),
			};
			T[(int32)EUEGT2Activity::Socialise] = {
				LOCTEXT("AnSoc1", "haven't seen you in an age"),
				LOCTEXT("AnSoc2", "stopping to talk. I've got a minute"),
				LOCTEXT("AnSoc3", "you'll want to hear this"),
				LOCTEXT("AnSoc4", "how's the family keeping?"),
				LOCTEXT("AnSoc5", "wait, tell me that bit again"),
			};
			T[(int32)EUEGT2Activity::Worship] = {
				LOCTEXT("AnWor1", "off to the church. it's about time"),
				LOCTEXT("AnWor2", "going to sit a while"),
				LOCTEXT("AnWor3", "the bell's gone. better move"),
				LOCTEXT("AnWor4", "candles want lighting"),
			};
			T[(int32)EUEGT2Activity::Play] = {
				LOCTEXT("AnPlay1", "race you to the well"),
				LOCTEXT("AnPlay2", "you're it!"),
				LOCTEXT("AnPlay3", "watch this bit"),
				LOCTEXT("AnPlay4", "not going in yet. it's still light"),
				LOCTEXT("AnPlay5", "bet I can get up there"),
			};
			T[(int32)EUEGT2Activity::Stroll] = {
				LOCTEXT("AnStr1", "walking off the morning"),
				LOCTEXT("AnStr2", "just stretching my legs"),
				LOCTEXT("AnStr3", "nowhere in particular"),
				LOCTEXT("AnStr4", "nice out. might go the long way round"),
			};
			T[(int32)EUEGT2Activity::Rest] = {
				LOCTEXT("AnRest1", "sitting down for five minutes"),
				LOCTEXT("AnRest2", "my feet are done"),
				LOCTEXT("AnRest3", "best bench in town, this"),
				LOCTEXT("AnRest4", "watching the world go by"),
			};
			T[(int32)EUEGT2Activity::Dinner] = {
				LOCTEXT("AnDin1", "supper's on. I can smell it from here"),
				LOCTEXT("AnDin2", "in for the evening"),
				LOCTEXT("AnDin3", "eating, then not moving"),
				LOCTEXT("AnDin4", "the pot's been on since four"),
			};
			T[(int32)EUEGT2Activity::Tavern] = {
				LOCTEXT("AnTav1", "one drink. one"),
				LOCTEXT("AnTav2", "off to the inn. join us"),
				LOCTEXT("AnTav3", "there's a fire lit and I intend to sit by it"),
				LOCTEXT("AnTav4", "someone owes me a round"),
				LOCTEXT("AnTav5", "we'll be singing by nine. sorry in advance"),
			};
			T[(int32)EUEGT2Activity::HomeTime] = {
				LOCTEXT("AnHome1", "that's me done. heading home"),
				LOCTEXT("AnHome2", "home before dark, for once"),
				LOCTEXT("AnHome3", "walking back the short way"),
				LOCTEXT("AnHome4", "long enough. home"),
			};
			T[(int32)EUEGT2Activity::Shelter] = {
				LOCTEXT("AnShel1", "getting under cover, this is coming down hard"),
				LOCTEXT("AnShel2", "nope. inside. now"),
				LOCTEXT("AnShel3", "waiting this one out"),
				LOCTEXT("AnShel4", "should have brought a coat"),
				LOCTEXT("AnShel5", "under the awning till it eases"),
			};
			T[(int32)EUEGT2Activity::Patrol] = {
				LOCTEXT("AnPat1", "walking the round. all quiet so far"),
				LOCTEXT("AnPat2", "checking the far end, then back"),
				LOCTEXT("AnPat3", "someone has to be awake at this hour"),
			};
			T[(int32)EUEGT2Activity::Idle] = {
				LOCTEXT("AnIdle1", "hm"),
				LOCTEXT("AnIdle2", "now where did I put it"),
				LOCTEXT("AnIdle3", "long day"),
			};

			// Everything unfilled falls back to the generic pool rather than
			// being empty: a missing row must never mean a silent NPC.
			const FPool Generic = {
				LOCTEXT("AnGen1", "getting on with it"),
				LOCTEXT("AnGen2", "somewhere to be"),
				LOCTEXT("AnGen3", "won't be long"),
			};
			for (FPool& Pool : T)
			{
				if (Pool.Num() == 0)
				{
					Pool = Generic;
				}
			}
			return T;
		}();
		return Table;
	}

	/** Reply: the player just talked to them. */
	const TArray<FPool>& ReplyTable()
	{
		static const TArray<FPool> Table = []
		{
			TArray<FPool> T;
			T.SetNum((int32)EUEGT2Activity::Count);

			T[(int32)EUEGT2Activity::Work] = {
				LOCTEXT("RpWork1", "busy, but go on"),
				LOCTEXT("RpWork2", "talk and I'll keep working, if that's all right"),
				LOCTEXT("RpWork3", "you've caught me mid-job"),
			};
			T[(int32)EUEGT2Activity::Sleep] = {
				LOCTEXT("RpSleep1", "...it is the middle of the night"),
				LOCTEXT("RpSleep2", "go away. politely"),
				LOCTEXT("RpSleep3", "whatever it is, morning"),
			};
			T[(int32)EUEGT2Activity::Market] = {
				LOCTEXT("RpMkt1", "buying or browsing?"),
				LOCTEXT("RpMkt2", "the far stall's cheaper. don't tell him I said"),
				LOCTEXT("RpMkt3", "hold this a moment"),
			};
			T[(int32)EUEGT2Activity::Tavern] = {
				LOCTEXT("RpTav1", "pull up a chair"),
				LOCTEXT("RpTav2", "you're buying, then?"),
				LOCTEXT("RpTav3", "this is my second. or fourth"),
			};
			T[(int32)EUEGT2Activity::Shelter] = {
				LOCTEXT("RpShel1", "budge up, there's room"),
				LOCTEXT("RpShel2", "you're soaked. get under here"),
				LOCTEXT("RpShel3", "it'll pass. they always do"),
			};
			T[(int32)EUEGT2Activity::Play] = {
				LOCTEXT("RpPlay1", "are you playing or watching?"),
				LOCTEXT("RpPlay2", "you're too big to hide anywhere good"),
				LOCTEXT("RpPlay3", "bet you can't"),
			};
			T[(int32)EUEGT2Activity::Rest] = {
				LOCTEXT("RpRest1", "sit down, you're making me tired"),
				LOCTEXT("RpRest2", "plenty of bench"),
				LOCTEXT("RpRest3", "I've been here since noon and I regret nothing"),
			};

			const FPool Generic = {
				LOCTEXT("RpGen1", "morning. afternoon. I've lost track"),
				LOCTEXT("RpGen2", "can't stop, but good to see you"),
				LOCTEXT("RpGen3", "you're not from the lanes, are you"),
				LOCTEXT("RpGen4", "mind how you go"),
				LOCTEXT("RpGen5", "there's not much happening. that's the appeal"),
				LOCTEXT("RpGen6", "ask at the square, someone there will know"),
			};
			for (FPool& Pool : T)
			{
				if (Pool.Num() == 0)
				{
					Pool = Generic;
				}
			}
			return T;
		}();
		return Table;
	}

	/** Idle: muttered to nobody. Shorter than the rest on purpose. */
	const FPool& IdlePool()
	{
		static const FPool Pool = {
			LOCTEXT("IdG1", "mm"),
			LOCTEXT("IdG2", "right, then"),
			LOCTEXT("IdG3", "where was I"),
			LOCTEXT("IdG4", "that's that sorted"),
			LOCTEXT("IdG5", "one of those days"),
			LOCTEXT("IdG6", "nearly forgot"),
		};
		return Pool;
	}

	/** Greetings, split by the part of the day they belong to. */
	const FPool& GreetPool(float Hour)
	{
		static const FPool Dawn = {
			LOCTEXT("GrDawn1", "you're up early too, then"),
			LOCTEXT("GrDawn2", "morning. barely"),
			LOCTEXT("GrDawn3", "first one I've seen today"),
		};
		static const FPool Morning = {
			LOCTEXT("GrMorn1", "morning"),
			LOCTEXT("GrMorn2", "morning. sleep all right?"),
			LOCTEXT("GrMorn3", "you're about early"),
			LOCTEXT("GrMorn4", "hello there"),
		};
		static const FPool Afternoon = {
			LOCTEXT("GrAft1", "afternoon"),
			LOCTEXT("GrAft2", "hello. warm one, isn't it"),
			LOCTEXT("GrAft3", "back again?"),
			LOCTEXT("GrAft4", "afternoon. mind the cart"),
		};
		static const FPool Evening = {
			LOCTEXT("GrEve1", "evening"),
			LOCTEXT("GrEve2", "evening. heading in soon myself"),
			LOCTEXT("GrEve3", "nearly dark. don't go far"),
			LOCTEXT("GrEve4", "evening to you"),
		};
		static const FPool Night = {
			LOCTEXT("GrNight1", "you're out late"),
			LOCTEXT("GrNight2", "everyone else is in bed, you know"),
			LOCTEXT("GrNight3", "quiet, this hour. I like it"),
			LOCTEXT("GrNight4", "watch your step, the lamps only go so far"),
		};

		if (Hour < 6.0f)   { return Dawn; }
		if (Hour < 12.0f)  { return Morning; }
		if (Hour < 17.5f)  { return Afternoon; }
		if (Hour < 21.0f)  { return Evening; }
		return Night;
	}

	/** Comment: about the weather, mostly, because that is what people do. */
	const FPool& CommentPool(EUEGT2Weather Weather, float Hour)
	{
		static const FPool Clear = {
			LOCTEXT("CmClear1", "not a cloud. it won't last"),
			LOCTEXT("CmClear2", "you can see the light from here on a day like this"),
			LOCTEXT("CmClear3", "good drying weather"),
			LOCTEXT("CmClear4", "make the most of it"),
		};
		static const FPool Cloudy = {
			LOCTEXT("CmCloud1", "bit of cloud coming over"),
			LOCTEXT("CmCloud2", "could go either way, this"),
			LOCTEXT("CmCloud3", "grey, but dry. I'll take it"),
		};
		static const FPool Overcast = {
			LOCTEXT("CmOver1", "close today. rain in it somewhere"),
			LOCTEXT("CmOver2", "sky's sitting right on the roofs"),
			LOCTEXT("CmOver3", "flat old light, this"),
		};
		static const FPool Foggy = {
			LOCTEXT("CmFog1", "can't see the church from the well"),
			LOCTEXT("CmFog2", "came in off the water an hour ago"),
			LOCTEXT("CmFog3", "walk slow. it swallows sound too"),
			LOCTEXT("CmFog4", "you appeared out of that like a ghost"),
		};
		static const FPool Storm = {
			LOCTEXT("CmStorm1", "that's set in for the night"),
			LOCTEXT("CmStorm2", "no boats out today, and rightly"),
			LOCTEXT("CmStorm3", "roof's leaking again. of course it is"),
			LOCTEXT("CmStorm4", "get inside, you'll catch your death"),
		};
		static const FPool NightFall = {
			LOCTEXT("CmNight1", "lamps are on. must be later than I thought"),
			LOCTEXT("CmNight2", "tide's turning. you can hear it from the square"),
			LOCTEXT("CmNight3", "quiet now the stalls are down"),
		};

		// After dark the hour is more interesting than the sky.
		if ((Hour >= 21.5f || Hour < 4.5f) && Weather != EUEGT2Weather::Storm)
		{
			return NightFall;
		}
		switch (Weather)
		{
		case EUEGT2Weather::Cloudy:   return Cloudy;
		case EUEGT2Weather::Overcast: return Overcast;
		case EUEGT2Weather::Foggy:    return Foggy;
		case EUEGT2Weather::Storm:    return Storm;
		default:                      return Clear;
		}
	}

	/**
	 * Trade-specific lines, used in place of the generic Announce pool when
	 * someone is doing the thing their trade is about. This is what stops a
	 * hundred "back at it" bubbles from all reading the same.
	 */
	const TArray<FPool>& RoleWorkTable()
	{
		static const TArray<FPool> Table = []
		{
			TArray<FPool> T;
			T.SetNum((int32)EUEGT2NPCRole::Count);

			T[(int32)EUEGT2NPCRole::Farmer] = {
				LOCTEXT("RwFarm1", "north field wants turning before the rain comes"),
				LOCTEXT("RwFarm2", "top acre first, then the fence by the lane"),
				LOCTEXT("RwFarm3", "if the weather holds we cut on the third day"),
				LOCTEXT("RwFarm4", "something's been at the hedge again"),
			};
			T[(int32)EUEGT2NPCRole::Fisher] = {
				LOCTEXT("RwFish1", "tide's right. taking her out past the point"),
				LOCTEXT("RwFish2", "nets first, then the pots"),
				LOCTEXT("RwFish3", "if it's running we'll be back by eleven"),
				LOCTEXT("RwFish4", "wind's wrong. short trip today"),
			};
			T[(int32)EUEGT2NPCRole::Merchant] = {
				LOCTEXT("RwMer1", "stall up before the square fills"),
				LOCTEXT("RwMer2", "half of this has to go today or it goes off"),
				LOCTEXT("RwMer3", "haggle if you like. you won't win"),
				LOCTEXT("RwMer4", "cart came in light this week"),
			};
			T[(int32)EUEGT2NPCRole::Baker] = {
				LOCTEXT("RwBak1", "oven's lit. first batch by four"),
				LOCTEXT("RwBak2", "everyone's asleep and I'm elbow deep in dough"),
				LOCTEXT("RwBak3", "smell that? that's the whole street awake in an hour"),
			};
			T[(int32)EUEGT2NPCRole::Innkeeper] = {
				LOCTEXT("RwInn1", "barrels down, glasses up, doors open"),
				LOCTEXT("RwInn2", "they'll all be in the moment it gets dark"),
				LOCTEXT("RwInn3", "somebody left a boot here last night"),
			};
			T[(int32)EUEGT2NPCRole::Priest] = {
				LOCTEXT("RwPri1", "the bell wants ringing and the roof wants mending"),
				LOCTEXT("RwPri2", "door's open, as it always is"),
				LOCTEXT("RwPri3", "quiet in there, if you need quiet"),
			};
			T[(int32)EUEGT2NPCRole::Smith] = {
				LOCTEXT("RwSmi1", "forge is hot. don't lean on anything"),
				LOCTEXT("RwSmi2", "two hinges and a plough share before dark"),
				LOCTEXT("RwSmi3", "if it's bent I can straighten it. mostly"),
			};
			T[(int32)EUEGT2NPCRole::Dockhand] = {
				LOCTEXT("RwDoc1", "whole boat to unload before the light goes"),
				LOCTEXT("RwDoc2", "mind the ropes. they take fingers"),
				LOCTEXT("RwDoc3", "crates in, crates out, crates in"),
			};
			T[(int32)EUEGT2NPCRole::Child] = {
				LOCTEXT("RwChi1", "supposed to be at lessons. am going. slowly"),
				LOCTEXT("RwChi2", "I know a way over the wall"),
				LOCTEXT("RwChi3", "the dog follows me everywhere now"),
			};
			T[(int32)EUEGT2NPCRole::Elder] = {
				LOCTEXT("RwEld1", "I've walked this road for sixty years. it's shorter now"),
				LOCTEXT("RwEld2", "same bench, same hour. don't fix what works"),
				LOCTEXT("RwEld3", "the harbour was half this size once"),
			};
			T[(int32)EUEGT2NPCRole::Clerk] = {
				LOCTEXT("RwCle1", "ninth floor, and the lift's out again"),
				LOCTEXT("RwCle2", "ledgers till five, then I'm nobody's problem"),
				LOCTEXT("RwCle3", "meeting at ten that could have been a note"),
			};
			T[(int32)EUEGT2NPCRole::Shopkeeper] = {
				LOCTEXT("RwSho1", "shutters up. let's see who's buying"),
				LOCTEXT("RwSho2", "stock came in wrong. all of it"),
				LOCTEXT("RwSho3", "open till eight, whatever the weather does"),
			};
			T[(int32)EUEGT2NPCRole::Courier] = {
				LOCTEXT("RwCou1", "three drops this side, then across the avenue"),
				LOCTEXT("RwCou2", "signed for, sealed, and late"),
				LOCTEXT("RwCou3", "I know this city by its back alleys"),
			};
			T[(int32)EUEGT2NPCRole::Officer] = {
				LOCTEXT("RwOff1", "quiet night. long may it last"),
				LOCTEXT("RwOff2", "the lamps down the west end are out again"),
				LOCTEXT("RwOff3", "move along is the whole of the job, mostly"),
			};
			T[(int32)EUEGT2NPCRole::Busker] = {
				LOCTEXT("RwBus1", "two songs and a hat. that's the trade"),
				LOCTEXT("RwBus2", "acoustics under the arch are worth the walk"),
				LOCTEXT("RwBus3", "requests cost extra"),
			};
			T[(int32)EUEGT2NPCRole::Gardener] = {
				LOCTEXT("RwGar1", "planters want water before the sun's high"),
				LOCTEXT("RwGar2", "somebody walked through the new bed. again"),
				LOCTEXT("RwGar3", "give it a season and you won't recognise it"),
			};
			T[(int32)EUEGT2NPCRole::Sailor] = {
				LOCTEXT("RwSai1", "loading out on the evening tide"),
				LOCTEXT("RwSai2", "six weeks at sea and the first thing I miss is bread"),
				LOCTEXT("RwSai3", "she needs caulking before the next run"),
			};
			return T;
		}();
		return Table;
	}

	/** Animals get a sound, in the same bubble, in a different tint. */
	const FPool& AnimalPool(EUEGT2NPCSpecies Species, EUEGT2Activity Activity)
	{
		static const FPool DogIdle = {
			LOCTEXT("AnimDog1", "*woof*"), LOCTEXT("AnimDog2", "*snuffle*"),
			LOCTEXT("AnimDog3", "*bark bark*"), LOCTEXT("AnimDog4", "*tail thumping*") };
		static const FPool DogPlay = {
			LOCTEXT("AnimDogP1", "*BARK*"), LOCTEXT("AnimDogP2", "*excited spinning*"),
			LOCTEXT("AnimDogP3", "*drops stick hopefully*") };
		static const FPool Cat = {
			LOCTEXT("AnimCat1", "*mrrp*"), LOCTEXT("AnimCat2", "*slow blink*"),
			LOCTEXT("AnimCat3", "*meow*"), LOCTEXT("AnimCat4", "*ignores you completely*") };
		static const FPool Chicken = {
			LOCTEXT("AnimChk1", "*buk buk*"), LOCTEXT("AnimChk2", "*bwuk!*"),
			LOCTEXT("AnimChk3", "*pecks at nothing*") };
		static const FPool Duck = {
			LOCTEXT("AnimDuk1", "*quack*"), LOCTEXT("AnimDuk2", "*quack quack*"),
			LOCTEXT("AnimDuk3", "*upends itself*") };
		static const FPool Sheep = {
			LOCTEXT("AnimShp1", "*baaa*"), LOCTEXT("AnimShp2", "*chewing*"),
			LOCTEXT("AnimShp3", "*stares blankly*") };
		static const FPool Cow = {
			LOCTEXT("AnimCow1", "*moo*"), LOCTEXT("AnimCow2", "*long slow moo*"),
			LOCTEXT("AnimCow3", "*swishes tail*") };
		static const FPool Pig = {
			LOCTEXT("AnimPig1", "*oink*"), LOCTEXT("AnimPig2", "*snorting happily*"),
			LOCTEXT("AnimPig3", "*rooting about*") };
		static const FPool Goat = {
			LOCTEXT("AnimGot1", "*maaa*"), LOCTEXT("AnimGot2", "*tries to eat your sleeve*"),
			LOCTEXT("AnimGot3", "*headbutts the fence*") };
		static const FPool Horse = {
			LOCTEXT("AnimHrs1", "*whinny*"), LOCTEXT("AnimHrs2", "*snort*"),
			LOCTEXT("AnimHrs3", "*stamps once*") };
		static const FPool Seagull = {
			LOCTEXT("AnimGul1", "*KRAAA*"), LOCTEXT("AnimGul2", "*screech*"),
			LOCTEXT("AnimGul3", "*eyeing your pockets*") };
		static const FPool Rabbit = {
			LOCTEXT("AnimRab1", "*twitch*"), LOCTEXT("AnimRab2", "*freezes*"),
			LOCTEXT("AnimRab3", "*thumps a foot*") };
		static const FPool Flee = {
			LOCTEXT("AnimFlee1", "*scatters*"), LOCTEXT("AnimFlee2", "*bolts*"),
			LOCTEXT("AnimFlee3", "*panicked flapping*") };

		if (Activity == EUEGT2Activity::Flee)
		{
			return Flee;
		}
		switch (Species)
		{
		case EUEGT2NPCSpecies::Dog:
			return (Activity == EUEGT2Activity::Play || Activity == EUEGT2Activity::Follow)
				? DogPlay : DogIdle;
		case EUEGT2NPCSpecies::Cat:     return Cat;
		case EUEGT2NPCSpecies::Chicken: return Chicken;
		case EUEGT2NPCSpecies::Duck:    return Duck;
		case EUEGT2NPCSpecies::Sheep:   return Sheep;
		case EUEGT2NPCSpecies::Cow:     return Cow;
		case EUEGT2NPCSpecies::Pig:     return Pig;
		case EUEGT2NPCSpecies::Goat:    return Goat;
		case EUEGT2NPCSpecies::Horse:   return Horse;
		case EUEGT2NPCSpecies::Seagull: return Seagull;
		default:                        return Rabbit;
		}
	}

	/** True when this role's trade lines should stand in for the generic ones. */
	bool UseTradeVoice(EUEGT2NPCRole Role, EUEGT2Activity Activity)
	{
		switch (Activity)
		{
		case EUEGT2Activity::Work:
		case EUEGT2Activity::Commute:
			return true;
		case EUEGT2Activity::Market:
			return Role == EUEGT2NPCRole::Merchant || Role == EUEGT2NPCRole::Fisher
				|| Role == EUEGT2NPCRole::Baker;
		case EUEGT2Activity::Patrol:
			return Role == EUEGT2NPCRole::Officer;
		case EUEGT2Activity::Play:
			return Role == EUEGT2NPCRole::Child;
		case EUEGT2Activity::Rest:
			return Role == EUEGT2NPCRole::Elder;
		default:
			return false;
		}
	}
}

const TArray<FText>& GetSpeechPool(EUEGT2NPCRole Role, EUEGT2NPCSpecies Species,
	EUEGT2Activity Activity, EUEGT2SpeechMood Mood, EUEGT2Weather Weather, float Hour)
{
	using namespace UEGT2Speech;

	if (IsAnimalSpecies(Species))
	{
		return AnimalPool(Species, Activity);
	}

	const int32 ActivityIndex = FMath::Clamp((int32)Activity, 0, (int32)EUEGT2Activity::Count - 1);

	switch (Mood)
	{
	case EUEGT2SpeechMood::Greet:
		return GreetPool(Hour);

	case EUEGT2SpeechMood::Comment:
		return CommentPool(Weather, Hour);

	case EUEGT2SpeechMood::Reply:
		return ReplyTable()[ActivityIndex];

	case EUEGT2SpeechMood::Idle:
		return IdlePool();

	default:
	{
		const int32 RoleIndex = FMath::Clamp((int32)Role, 0, (int32)EUEGT2NPCRole::Count - 1);
		const TArray<FText>& Trade = RoleWorkTable()[RoleIndex];
		if (Trade.Num() > 0 && UseTradeVoice(Role, Activity))
		{
			return Trade;
		}
		return AnnounceTable()[ActivityIndex];
	}
	}
}

FText GetSpeechLine(EUEGT2NPCRole Role, EUEGT2NPCSpecies Species, EUEGT2Activity Activity,
	EUEGT2SpeechMood Mood, EUEGT2Weather Weather, float Hour, uint32 Seed, uint32 Variation)
{
	const TArray<FText>& Pool = GetSpeechPool(Role, Species, Activity, Mood, Weather, Hour);
	if (Pool.Num() == 0)
	{
		// GetSpeechPool never returns an empty pool, and the test suite asserts
		// it, but a silent NPC would be a very quiet bug so guard it anyway.
		return FText::GetEmpty();
	}
	const uint32 Index = UEGT2HashSeed(Seed, (uint32)Activity * 31u + (uint32)Mood, Variation);
	return Pool[Index % (uint32)Pool.Num()];
}

#undef LOCTEXT_NAMESPACE
