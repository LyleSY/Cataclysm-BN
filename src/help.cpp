#include "help.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>
#include <list>
#include <numeric>
#include <optional>
#include <vector>

#include "action.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "color.h"
#include "cursesdef.h"
#include "debug.h"
#include "fstream_utils.h"
#include "input.h"
#include "json.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "string_formatter.h"
#include "string_utils.h"
#include "text_snippets.h"
#include "translations.h"
#include "ui_manager.h"
#include "path_display.h"

help &get_help()
{
    static help single_instance;
    return single_instance;
}

void help::load()
{
    read_from_file_json( PATH_INFO::help(), [&]( JsonIn & jsin ) {
        deserialize( jsin );
    }, true );
}

void help::deserialize( JsonIn &jsin )
{
    hotkeys.clear();

    std::string note_colors = get_note_colors();
    std::string dir_grid = get_dir_grid();

    jsin.start_array();
    while( !jsin.end_array() ) {
        JsonObject jo = jsin.get_object();

        std::vector<std::string> messages;
        jo.read( "messages", messages );

        for( auto &line : messages ) {
            if( line == "<DRAW_NOTE_COLORS>" ) {
                line = replace_all( line, "<DRAW_NOTE_COLORS>", note_colors );
                continue;
            } else if( line == "<HELP_DRAW_DIRECTIONS>" ) {
                line = replace_all( line, "<HELP_DRAW_DIRECTIONS>", dir_grid );
                continue;
            } else if( line == "<GAME_DIRECTORIES>" ) {
                line = resolved_game_paths();
            }
        }

        jo.get_string( "type" ); // Mark as visited
        std::string name = jo.get_string( "name" );
        help_texts[jo.get_int( "order" )] = std::make_pair( name, messages );
        hotkeys.push_back( get_hotkeys( name ) );
    }
}

std::string help::get_dir_grid()
{
    static const std::array<action_id, 9> movearray = {{
            ACTION_MOVE_FORTH_LEFT, ACTION_MOVE_FORTH, ACTION_MOVE_FORTH_RIGHT,
            ACTION_MOVE_LEFT,  ACTION_PAUSE,  ACTION_MOVE_RIGHT,
            ACTION_MOVE_BACK_LEFT, ACTION_MOVE_BACK, ACTION_MOVE_BACK_RIGHT
        }
    };

    std::string movement = "<LEFTUP_0>  <UP_0>  <RIGHTUP_0>   <LEFTUP_1>  <UP_1>  <RIGHTUP_1>\n"
                           " \\ | /     \\ | /\n"
                           "  \\|/       \\|/\n"
                           "<LEFT_0>--<pause_0>--<RIGHT_0>   <LEFT_1>--<pause_1>--<RIGHT_1>\n"
                           "  /|\\       /|\\\n"
                           " / | \\     / | \\\n"
                           "<LEFTDOWN_0>  <DOWN_0>  <RIGHTDOWN_0>   <LEFTDOWN_1>  <DOWN_1>  <RIGHTDOWN_1>";

    for( auto dir : movearray ) {
        std::vector<char> keys = keys_bound_to( dir );
        for( size_t i = 0; i < 2; i++ ) {
            std::string what = "<" + action_ident( dir ) + string_format( "_%d>", i );
            std::string with = i < keys.size()
                               ? string_format( "<color_light_blue>%s</color>", keys[i] )
                               : "<color_red>?</color>";
            movement = replace_all( movement, what, with );
        }
    }

    return movement;
}

void help::draw_menu( const catacurses::window &win )
{
    werase( win );
    // NOLINTNEXTLINE(cata-use-named-point-constants)
    int y = fold_and_print( win, point( 1, 0 ), getmaxx( win ) - 2, c_white,
                            _( "Please press one of the following for help on that topic:\n"
                               "Press ESC to return to the game." ) ) + 1;

    size_t half_size = help_texts.size() / 2 + 1;
    int second_column = divide_round_up( getmaxx( win ), 2 );
    for( size_t i = 0; i < help_texts.size(); i++ ) {
        std::string cat_name = _( help_texts[i].first );
        if( i < half_size ) {
            second_column = std::max( second_column, utf8_width( cat_name ) + 4 );
        }

        shortcut_print( win, point( i < half_size ? 1 : second_column, y + i % half_size ),
                        c_white, c_light_blue, cat_name );
    }

    wnoutrefresh( win );
}

std::string help::get_note_colors()
{
    std::string text = _( "Note colors: " );
    for( const auto &color_pair : get_note_color_names() ) {
        // The color index is not translatable, but the name is.
        text += string_format( "%s:%s, ", colorize( color_pair.first, get_note_color( color_pair.first ) ),
                               _( color_pair.second ) );
    }

    return text;
}

void help::display_help()
{
    catacurses::window w_help_border;
    catacurses::window w_help;

    ui_adaptor ui;
    const auto init_windows = [&]( ui_adaptor & ui ) {
        w_help_border = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH,
                                            point( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                                                    TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 ) );
        w_help = catacurses::newwin( FULL_SCREEN_HEIGHT - 2, FULL_SCREEN_WIDTH - 2,
                                     point( 1 + ( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0 ),
                                            1 + ( TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 ) ) );
        ui.position_from_window( w_help_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    ctxt.register_cardinal();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "CONFIRM" );
    // for the menu shortcuts
    ctxt.register_action( "ANY_INPUT" );

    std::string action;

    ui.on_redraw( [&]( const ui_adaptor & ) {
        draw_border( w_help_border, BORDER_COLOR, _( " HELP " ), c_black_white );
        wnoutrefresh( w_help_border );
        draw_menu( w_help );
    } );

    do {
        ui_manager::redraw();

        action = ctxt.handle_input();
        std::string sInput = ctxt.get_raw_input().text;
        for( size_t i = 0; i < hotkeys.size(); ++i ) {
            for( const std::string &hotkey : hotkeys[i] ) {
                if( sInput == hotkey ) {
                    std::vector<std::string> i18n_help_texts;
                    i18n_help_texts.reserve( help_texts[i].second.size() );
                    std::transform( help_texts[i].second.begin(), help_texts[i].second.end(),
                    std::back_inserter( i18n_help_texts ), [&]( std::string & line ) {
                        std::string line_proc = _( line );
                        size_t pos = line_proc.find( "<press_", 0, 7 );
                        while( pos != std::string::npos ) {
                            size_t pos2 = line_proc.find( ">", pos, 1 );

                            std::string action = line_proc.substr( pos + 7, pos2 - pos - 7 );
                            auto replace = "<color_light_blue>" + press_x( look_up_action( action ), "", "" ) + "</color>";

                            if( replace.empty() ) {
                                debugmsg( "Help json: Unknown action: %s", action );
                            } else {
                                line_proc = replace_all( line_proc, "<press_" + action + ">", replace );
                            }

                            pos = line_proc.find( "<press_", pos2, 7 );
                        }
                        return line_proc;
                    } );

                    if( !i18n_help_texts.empty() ) {
                        ui.on_screen_resize( nullptr );

                        const auto get_w_help_border = [&]() {
                            init_windows( ui );
                            return w_help_border;
                        };

                        scrollable_text( get_w_help_border, _( " HELP " ),
                                         std::accumulate( i18n_help_texts.begin() + 1, i18n_help_texts.end(),
                                                          i18n_help_texts.front(),
                        []( const std::string & lhs, const std::string & rhs ) {
                            return lhs + "\n\n" + rhs;
                        } ) );

                        ui.on_screen_resize( init_windows );
                    }
                    action = "CONFIRM";
                    break;
                }
            }
        }
    } while( action != "QUIT" );
}

std::string get_hint()
{
    return SNIPPET.random_from_category( "hint" ).value_or( translation() ).translated();
}
