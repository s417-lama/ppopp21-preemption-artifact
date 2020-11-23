prefix = "noop"
# prefix = "noop_knl"

yrange = [0, 0.085]

showlegend = true
# showlegend = false

no_preemption_dir = "results/#{prefix}_no_preemption/latest"

data_dirs = [
    "results/#{prefix}_preemption_klts/latest",
    "results/#{prefix}_preemption_klts_futex/latest",
    "results/#{prefix}_preemption_klts_futex_local/latest",
    "results/#{prefix}_preemption_yield/latest",
    "results/#{prefix}_preemption_disabled/latest",
]

opts = [
    {"KLT-switching"                       , "#1f77b4", "dash"   , "x-thin"      },
    {"KLT-switching<br>(futex)"            , "#1f77b4", "dashdot", "cross-thin"  },
    {"KLT-switching<br>(futex, local pool)", "#1f77b4", "solid"  , "diamond-open"},
    {"Signal-yield"                        , "#ff7f0e", "solid"  , "circle-open" },
    {"Timer interruption<br>only"          , "#2ca02c", "dot"    , "square-open" },
]

filename = System.argv() |> Enum.at(0)

linewidth = 2

get_data_in_file = fn path ->
    intvl =
        File.stream!(path)
        |> Stream.map(&String.split/1)
        |> Stream.map(fn
            ["Preemption", "timer", "interval:", intvl, _] -> intvl
            _ -> nil
        end)
        |> Stream.filter(&!is_nil(&1))
        |> Enum.to_list()
        |> case do
            [] -> nil
            [intvl] -> String.to_integer(intvl)
        end
    [_ | times] =
        File.stream!(path)
        |> Stream.map(&String.split/1)
        |> Stream.map(fn
            [_, _, _, "[elapsed]", time, _] -> String.to_integer(time) / 1_000_000_000
            _ -> nil
        end)
        |> Stream.filter(&!is_nil(&1))
        |> Enum.to_list()
    {intvl, times}
end

get_data_in_dir = fn path ->
    "#{path}/*.out"
    |> Path.expand()
    |> Path.wildcard()
    |> Enum.map(get_data_in_file)
    |> Enum.group_by(fn {intvl, _} -> intvl end, fn {_, times} -> times end)
    |> Enum.map(fn {intvl, times} -> {intvl, List.flatten(times)} end)
end

[{_, no_preemption_times}] = get_data_in_dir.(no_preemption_dir)
no_preemption_y = Statistics.mean(no_preemption_times)

Enum.zip(data_dirs, opts)
|> Enum.map(fn {dir, opt} ->
    {title, color, dash, symbol} = opt
    {xs, times} =
        get_data_in_dir.(dir)
        |> Enum.sort()
        |> Enum.unzip()
    {ys, errors} =
        times
        |> Enum.map(fn t -> Enum.map(t, fn x -> x / no_preemption_y - 1 end) end)
        |> Enum.map(fn t -> {Statistics.mean(t), Statistics.stdev(t)} end)
        |> Enum.unzip()
    %{
        x: xs,
        y: ys,
        name: title,
        error_y: %{
            type:      "data",
            array:     errors,
            visible:   true,
            thickness: linewidth,
        },
        line: %{
            width: linewidth,
            color: color,
            dash: dash,
        },
        marker: %{
            symbol: symbol,
            size:   14,
            line:   %{width: linewidth, color: color},
        },
    }
end)
|> PlotlyEx.plot(%{
    width:  500,
    height: 400,
    xaxis: %{
        type:  "log",
        showgrid: true,
        dtick: "D1",
        title: %{text: "Timer interval (&#x03BC;s)"},
        tickangle: 0,
        showline: true,
        ticks: "outside",
        tickvals: [100,200,300,400,500,600,700,800,900,1000,2000,3000,4000,5000,6000,7000,8000,9000,10000],
        ticktext: [100,"","","","","","","","",1000,"","","","","","","","",10000],
        range:    [2, 4.1],
    },
    yaxis: %{
        title: %{text: "Overhead"},
        tickformat: ",.0%",
        showline: true,
        range: yrange,
    },
    legend: %{
        x: 0.5,
        y: 1,
    },
    font: %{
        size: 22,
    },
    margin: %{
        l: 75,
        r: 10,
        b: 70,
        t: 0,
        pad: 0,
    },
    showlegend: showlegend,
})
|> PlotlyEx.show(filename: filename)
